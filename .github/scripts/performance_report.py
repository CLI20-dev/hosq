#!/usr/bin/env python3
"""Publish bounded numeric data from an unprivileged benchmark workflow.

This file runs only from the default branch. Never import code or render Markdown
from a PR checkout or an artifact. Measurements themselves remain PR-controlled.
"""
import io
import json
import math
import os
from pathlib import Path
import re
import urllib.error
import urllib.request
import zipfile

CASES = (
    "id_only", "partial_fields", "all_fields", "all_fields_randomize",
    "id_only_pretty", "partial_fields_pretty", "all_fields_pretty",
    "all_fields_randomize_pretty",
)
MARKER = "<!-- hosq-performance-v1 -->"
WORKFLOW = ".github/workflows/performance.yml"
MAX_ARCHIVE = 8 * 1024 * 1024
MAX_REPORT = 128 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def positive(value):
    return type(value) in (int, float) and math.isfinite(value) and 0 < value < 1e15


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON key")
        result[key] = value
    return result


def read_report(archive):
    require(len(archive) <= MAX_ARCHIVE, "Artifact too large")
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        members = zipped.infolist()
        require(len(members) <= 256, "Too many artifact files")
        reports = [item for item in members if item.filename == "report.json"]
        require(len(reports) == 1, "Expected one report.json at artifact root")
        report = reports[0]
        require(0 < report.file_size <= MAX_REPORT, "Report too large or empty")
        # Nothing is extracted, and only this bounded JSON member is read.
        data = zipped.read(report)
        require(len(data) <= MAX_REPORT, "Report too large")
    return json.loads(data, object_pairs_hook=unique_object)


def validate_report(report, repository, run):
    require(type(report) is dict and type(report.get("schema_version")) is int
            and report["schema_version"] == 1, "Unknown report schema")
    require(report.get("repository") == repository, "Wrong repository")
    require(type(report.get("run_id")) is int and report["run_id"] == run["id"], "Wrong run")
    for key in ("base_sha", "head_sha"):
        require(type(report.get(key)) is str and re.fullmatch(r"[0-9a-f]{40}", report[key]), "Invalid SHA")
    require(report["head_sha"] == run["head_sha"], "Wrong head SHA")
    rows = report.get("cases")
    require(type(rows) is list and len(rows) == len(CASES), "Missing workloads")
    seen = set()
    for row in rows:
        require(type(row) is dict and row.get("name") in CASES, "Unknown workload")
        require(row["name"] not in seen, "Duplicate workload")
        seen.add(row["name"])
        require(integer(row.get("bytes"), 1, 1_000_000_000), "Invalid input size")
        require(type(row.get("input_hash")) is str and re.fullmatch(r"[0-9]{1,20}", row["input_hash"]), "Invalid input hash")
        require(int(row["input_hash"]) < 2**64, "Invalid input hash")
        for side in ("base", "head"):
            stats = row.get(side)
            require(type(stats) is dict, "Missing statistics")
            require(positive(stats.get("cpu_ns")) and positive(stats.get("wall_ns")), "Invalid timing")
            cv = stats.get("cv_percent")
            require(type(cv) in (int, float) and math.isfinite(cv) and 0 <= cv < 1e6, "Invalid variation")
            require(stats.get("processes") == 2 and type(stats["processes"]) is int, "Expected two ABBA processes")
            require(integer(stats.get("samples"), 2, 200) and stats["samples"] % 2 == 0, "Invalid sample count")
        require(row["base"]["samples"] == row["head"]["samples"], "Unequal sample counts")
        delta = row["head"]["cpu_ns"] / row["base"]["cpu_ns"] - 1
        require(math.isfinite(delta * 100), "Unrepresentable timing ratio")
    require(seen == set(CASES), "Missing workloads")
    return report


def eligible_run(run, repository, repository_id):
    return (
        run.get("event") == "pull_request" and run.get("status") == "completed"
        and run.get("conclusion") == "success"
        and run.get("name") == "PR Performance"
        and run.get("path", "").split("@")[0] == WORKFLOW
        and run.get("repository", {}).get("id") == repository_id
        and run.get("repository", {}).get("full_name") == repository
    )


def matching_pr(pr, report, run, repository_id):
    if not (
        pr.get("state") == "open"
        and pr.get("base", {}).get("repo", {}).get("id") == repository_id
        and pr.get("head", {}).get("sha") == report["head_sha"] == run["head_sha"]
    ):
        return False
    associated = run.get("pull_requests", [])
    if associated:
        return any(
            item.get("number") == pr["number"]
            and item.get("head", {}).get("sha") == report["head_sha"]
            and item.get("base", {}).get("sha") == report["base_sha"]
            for item in associated
        )
    # GitHub sometimes omits run.pull_requests (including forks). The commit's
    # associated-PR API supplies candidates; require its current base as well.
    return pr.get("base", {}).get("sha") == report["base_sha"]


def render(report, repository, server="https://github.com"):
    base, head, run_id = report["base_sha"], report["head_sha"], report["run_id"]
    root = f"{server}/{repository}"
    lines = [MARKER, f"<!-- run-id: {run_id} -->", "### HOSQ performance: PR base → head", "",
             f"[{base[:12]}]({root}/commit/{base}) → [{head[:12]}]({root}/commit/{head}) · [measurement run]({root}/actions/runs/{run_id})", "",
             "Same runner, compiler, inputs, and harness; separate base/head/head/base processes. Medians shown below; positive change means slower.", "",
             "| Workload | Base CPU µs | Head CPU µs | CPU change | Base wall µs | Head wall µs | CPU CV base/head |",
             "|---|---:|---:|---:|---:|---:|---:|"]
    rows = {row["name"]: row for row in report["cases"]}
    for name in CASES:
        row = rows[name]
        b, h = row["base"], row["head"]
        delta = (h["cpu_ns"] / b["cpu_ns"] - 1) * 100
        lines.append(f"| {name} | {b['cpu_ns']/1000:.2f} | {h['cpu_ns']/1000:.2f} | {delta:+.2f}% | {b['wall_ns']/1000:.2f} | {h['wall_ns']/1000:.2f} | {b['cv_percent']:.2f}% / {h['cv_percent']:.2f}% |")
    counts = sorted({row["base"]["samples"] for row in rows.values()})
    lines += ["", f"Samples per revision/workload: {', '.join(map(str, counts))} across two processes. CV = sample CPU standard deviation / mean.", "",
              "Report only: no regression threshold. Hosted-runner noise can dominate small differences; repetitions within a process are correlated. These eight valid-JSON workloads are not a complete API or safety assessment. Measurements come from PR-controlled code; the publisher validates their structure and identities, not their truth."]
    return "\n".join(lines) + "\n"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, newurl):
        return None


class GitHub:
    def __init__(self, repository, token):
        self.root = os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")
        self.repository = repository
        self.token = token

    def request(self, path, method="GET", data=None):
        body = None if data is None else json.dumps(data).encode()
        request = urllib.request.Request(self.root + path, data=body, method=method, headers={
            "Authorization": f"Bearer {self.token}", "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28", "Content-Type": "application/json",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    def pages(self, path):
        separator = "&" if "?" in path else "?"
        for page in range(1, 11):
            rows = self.request(f"{path}{separator}per_page=100&page={page}")
            yield from rows
            if len(rows) < 100:
                return
        raise ValueError("Too many API results")

    def artifact(self, artifact_id):
        path = f"/repos/{self.repository}/actions/artifacts/{artifact_id}/zip"
        request = urllib.request.Request(self.root + path, headers={"Authorization": f"Bearer {self.token}"})
        # Strip API credentials before following the signed storage redirect.
        try:
            response = urllib.request.build_opener(NoRedirect).open(request, timeout=30)
        except urllib.error.HTTPError as error:
            require(error.code in (301, 302, 303, 307, 308), "Artifact download failed")
            location = error.headers.get("Location", "")
            require(location.startswith("https://"), "Invalid artifact storage URL")
            response = urllib.request.urlopen(location, timeout=30)
        with response:
            data = response.read(MAX_ARCHIVE + 1)
        require(len(data) <= MAX_ARCHIVE, "Artifact too large")
        return data


def existing_comment(comments):
    matches = [comment for comment in comments
               if comment.get("user", {}).get("type") == "Bot"
               and comment["user"].get("login") == "github-actions[bot]"
               and comment.get("body", "").startswith(MARKER + "\n")]
    return max(matches, key=lambda item: item["id"], default=None)


def publish(api, event, repository):
    repository_id = event["repository"]["id"]
    run_id = event["workflow_run"]["id"]
    require(type(run_id) is int, "Invalid workflow run ID")
    root = f"/repos/{repository}"
    run = api.request(f"{root}/actions/runs/{run_id}")
    if not eligible_run(run, repository, repository_id):
        return "Skipped ineligible measurement run."
    artifacts = api.request(f"{root}/actions/runs/{run_id}/artifacts?per_page=100")
    found = [a for a in artifacts["artifacts"] if a["name"] == "performance-report" and not a["expired"]]
    require(len(found) == 1 and artifacts["total_count"] <= 100, "Expected one measurement artifact")
    artifact = found[0]
    require(integer(artifact["size_in_bytes"], 1, MAX_ARCHIVE), "Artifact too large or empty")
    report = validate_report(read_report(api.artifact(artifact["id"])), repository, run)
    associated = run.get("pull_requests", [])
    if associated:
        numbers = {item["number"] for item in associated}
    else:
        numbers = {item["number"] for item in api.pages(f"{root}/commits/{run['head_sha']}/pulls")}
    candidates = [api.request(f"{root}/pulls/{number}") for number in sorted(numbers)]
    candidates = [pr for pr in candidates if matching_pr(pr, report, run, repository_id)]
    if len(candidates) != 1:
        return "Skipped: no unique open PR with the measured base and current head."
    number = candidates[0]["number"]
    comment = existing_comment(api.pages(f"{root}/issues/{number}/comments"))
    if comment:
        previous = re.search(r"<!-- run-id: (\d+) -->", comment["body"])
        if previous and int(previous[1]) > run_id:
            return "Skipped older measurement run."
    # Recheck after downloads and comment pagination, immediately before writing.
    if not matching_pr(api.request(f"{root}/pulls/{number}"), report, run, repository_id):
        return "Skipped: PR changed while preparing the comment."
    body = render(report, repository)
    if comment:
        api.request(f"{root}/issues/comments/{comment['id']}", "PATCH", {"body": body})
    else:
        api.request(f"{root}/issues/{number}/comments", "POST", {"body": body})
    return f"Updated performance comment on PR #{number}."


def main():
    repository = os.environ["GITHUB_REPOSITORY"]
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository), "Invalid repository")
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    message = publish(GitHub(repository, os.environ["GH_TOKEN"]), event, repository)
    print(message)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as stream:
            stream.write(message + "\n")


if __name__ == "__main__":
    main()
