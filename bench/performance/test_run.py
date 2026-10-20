import copy
import unittest
from run import CASES, analyze, verification, verify_equal


class AnalysisTests(unittest.TestCase):
    def fixture(self, cpu=(100, 200), wall=(150, 250)):
        return {'benchmarks': [dict(name='id_only', run_name='id_only', run_type='iteration',
                                    cpu_time=c, real_time=w, time_unit='ns') for c, w in zip(cpu, wall)] +
                [dict(name='id_only_mean', run_type='aggregate', cpu_time=999)]}

    def test_medians_ignore_aggregate_and_combine_processes(self):
        result = analyze([self.fixture(), self.fixture((300, 400), (350, 450))], 'id_only', 2)
        self.assertEqual(result['cpu_ns'], 250)
        self.assertEqual(result['wall_ns'], 300)
        self.assertEqual((result['samples'], result['processes']), (4, 2))
        self.assertAlmostEqual(result['cv_percent'], 51.6397779494)

    def test_bad_measurements_fail(self):
        for value in (float('nan'), float('inf'), 0, -1):
            with self.subTest(value=value), self.assertRaises(ValueError):
                analyze([self.fixture((value, 2))], 'id_only', 2)
        with self.assertRaises(ValueError):
            analyze([self.fixture()], 'all_fields', 2)
        with self.assertRaises(ValueError):
            analyze([self.fixture()], 'id_only', 3)

    def test_units_normalized(self):
        doc = self.fixture()
        for row in doc['benchmarks'][:2]: row['time_unit'] = 'us'
        self.assertEqual(analyze([doc], 'id_only', 2)['cpu_ns'], 150000)

    def test_verification_requires_identical_complete_records(self):
        text = '\n'.join(f'{name} bytes=42 fnv1a64=123 checksum=3,4,' for name in CASES)
        base = verification(text)
        verify_equal(base, copy.deepcopy(base))
        for field in ('bytes', 'input_hash', 'checksum'):
            altered = copy.deepcopy(base)
            altered['id_only'][field] = 'changed'
            with self.assertRaises(ValueError): verify_equal(base, altered)
        for broken in (text + '\n' + text.splitlines()[0], '\n'.join(text.splitlines()[1:]), text + '\ninvalid'):
            with self.assertRaises(ValueError): verification(broken)


if __name__ == '__main__': unittest.main()
