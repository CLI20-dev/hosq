import copy
import io
import json
import unittest
import zipfile

import performance_report as p

REPO = "CLI20-dev/hosq"
BASE, HEAD = "a" * 40, "b" * 40


def report():
    stats = dict(cpu_ns=100_000, wall_ns=105_000, cv_percent=2.5, samples=10, processes=2)
    return dict(schema_version=1, repository=REPO, base_sha=BASE, head_sha=HEAD, run_id=42,
                cases=[dict(name=name, bytes=493_912, input_hash="12345",
                            base=stats.copy(), head=dict(stats, cpu_ns=110_000)) for name in p.CASES])


def archive(data):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as zipped:
        zipped.writestr("report.json", json.dumps(data))
    return output.getvalue()


class FakeAPI:
    def __init__(self):
        self.run = dict(id=42, event="pull_request", status="completed", conclusion="success",
                        name="PR Performance", path=p.WORKFLOW, head_sha=HEAD,
                        repository=dict(id=1, full_name=REPO), pull_requests=[])
        self.pr = dict(number=7, state="open", head=dict(sha=HEAD), base=dict(sha=BASE, repo=dict(id=1)))
        self.report = report()
        self.comments = []
        self.writes = []
        self.pr_reads = 0
        self.change_before_write = False

    def request(self, path, method="GET", data=None):
        if method != "GET":
            self.writes.append((path, method, data))
            return {}
        if path.endswith("/actions/runs/42"):
            return self.run
        if path.endswith("/artifacts?per_page=100"):
            return dict(total_count=1, artifacts=[dict(id=99, name="performance-report", expired=False, size_in_bytes=1024)])
        if path.endswith("/pulls/7"):
            self.pr_reads += 1
            if self.change_before_write and self.pr_reads > 1:
                return dict(self.pr, head=dict(sha="c" * 40))
            return self.pr
        raise AssertionError(path)

    def pages(self, path):
        if "/commits/" in path:
            return [dict(number=7)]
        if path.endswith("/comments"):
            return self.comments
        raise AssertionError(path)

    def artifact(self, artifact_id):
        assert artifact_id == 99
        return archive(self.report)


class PublisherTests(unittest.TestCase):
    event = dict(repository=dict(id=1), workflow_run=dict(id=42))

    def test_valid_report_and_numeric_rendering(self):
        api = FakeAPI()
        value = p.validate_report(p.read_report(archive(report())), REPO, api.run)
        body = p.render(value, REPO)
        self.assertIn("+10.00%", body)
        self.assertIn("100.00 | 110.00", body)
        self.assertIn(f"/commit/{HEAD}", body)
        self.assertEqual(body.count("| id_only |"), 1)
        value["protocol"] = "@everyone <script>injected Markdown</script>"
        self.assertEqual(body, p.render(value, REPO))

    def test_rejects_mismatched_identity_and_invalid_numbers(self):
        changes = (
            lambda r: r.update(repository="elsewhere/repo"),
            lambda r: r.update(run_id=41),
            lambda r: r.update(head_sha="c" * 40),
            lambda r: r.update(base_sha="@someone"),
            lambda r: r["cases"].pop(),
            lambda r: r["cases"][0].update(name="@someone"),
            lambda r: r["cases"][0]["head"].update(cpu_ns=float("nan")),
            lambda r: r["cases"][0]["head"].update(wall_ns=float("inf")),
            lambda r: r["cases"][0]["head"].update(cpu_ns=0),
            lambda r: r["cases"][0]["head"].update(cpu_ns=True),
            lambda r: r["cases"][0]["head"].update(cv_percent=-1),
            lambda r: r["cases"][0]["head"].update(samples=9),
            lambda r: r["cases"][0]["head"].update(processes=True),
            lambda r: r["cases"][0].update(input_hash=str(2**64)),
        )
        for change in changes:
            with self.subTest(change=change):
                value = report()
                change(value)
                with self.assertRaises(ValueError):
                    p.validate_report(value, REPO, FakeAPI().run)

    def test_rejects_duplicate_workloads_and_json_keys(self):
        value = report()
        value["cases"][0] = copy.deepcopy(value["cases"][1])
        with self.assertRaises(ValueError):
            p.validate_report(value, REPO, FakeAPI().run)
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as zipped:
            zipped.writestr("report.json", '{"schema_version":1,"schema_version":1}')
        with self.assertRaises(ValueError):
            p.read_report(output.getvalue())

    def test_reads_only_bounded_exact_member_without_extracting(self):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as zipped:
            zipped.writestr("../payload.py", "raise RuntimeError('must not run')")
            zipped.writestr("report.json", json.dumps(report()))
        self.assertEqual(p.read_report(output.getvalue()), report())
        for name, data in (("nested/report.json", "{}"), ("report.json", " " * (p.MAX_REPORT + 1))):
            output = io.BytesIO()
            with zipfile.ZipFile(output, "w") as zipped:
                zipped.writestr(name, data)
            with self.assertRaises(ValueError):
                p.read_report(output.getvalue())

    def test_commit_association_fallback_publishes_current_pr(self):
        api = FakeAPI()
        self.assertIn("PR #7", p.publish(api, self.event, REPO))
        self.assertEqual(len(api.writes), 1)
        self.assertEqual(api.writes[0][1], "POST")

    def test_rejects_stale_heads_wrong_repository_and_changed_base(self):
        for change in (
            lambda api: api.pr["head"].update(sha="c" * 40),
            lambda api: api.pr["base"]["repo"].update(id=2),
            lambda api: api.pr["base"].update(sha="c" * 40),
            lambda api: api.pr.update(state="closed"),
        ):
            api = FakeAPI()
            change(api)
            self.assertIn("Skipped", p.publish(api, self.event, REPO))
            self.assertFalse(api.writes)

    def test_associated_pr_uses_base_at_measurement_time(self):
        api = FakeAPI()
        api.run["pull_requests"] = [dict(number=7, head=dict(sha=HEAD), base=dict(sha=BASE))]
        api.pr["base"]["sha"] = "c" * 40
        self.assertIn("PR #7", p.publish(api, self.event, REPO))
        self.assertEqual(len(api.writes), 1)
        api.run["pull_requests"][0]["base"]["sha"] = "d" * 40
        api.writes.clear()
        self.assertIn("Skipped", p.publish(api, self.event, REPO))
        self.assertFalse(api.writes)

    def test_rechecks_pr_immediately_before_writing(self):
        api = FakeAPI()
        api.change_before_write = True
        self.assertIn("PR changed", p.publish(api, self.event, REPO))
        self.assertFalse(api.writes)

    def test_updates_only_matching_bot_comment(self):
        api = FakeAPI()
        human = dict(id=1, body=p.MARKER + "\nhuman text", user=dict(login="human", type="User"))
        api.comments = [human]
        p.publish(api, self.event, REPO)
        self.assertEqual(api.writes[0][1], "POST")
        api.comments.append(dict(id=2, body=p.MARKER + "\nbot text", user=dict(login="github-actions[bot]", type="Bot")))
        api.writes.clear()
        p.publish(api, self.event, REPO)
        self.assertEqual(api.writes[0][1], "PATCH")
        self.assertTrue(api.writes[0][0].endswith("/comments/2"))

    def test_skips_older_run_and_ineligible_workflow(self):
        api = FakeAPI()
        api.comments = [dict(id=2, body=p.MARKER + "\n<!-- run-id: 43 -->", user=dict(login="github-actions[bot]", type="Bot"))]
        self.assertIn("older", p.publish(api, self.event, REPO))
        self.assertFalse(api.writes)
        for change in (dict(event="push"), dict(conclusion="failure"), dict(path=".github/workflows/other.yml"), dict(repository=dict(id=2, full_name=REPO))):
            api = FakeAPI()
            api.run.update(change)
            self.assertIn("ineligible", p.publish(api, self.event, REPO))
            self.assertFalse(api.writes)


if __name__ == "__main__":
    unittest.main()
