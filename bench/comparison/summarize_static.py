#!/usr/bin/env python3
"""Combine independent runs without treating their range as a confidence interval."""
import argparse
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run1', type=Path)
    parser.add_argument('run2', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    runs = [{(r['case'], r['engine']): r for r in json.loads((p/'summary.json').read_text())}
            for p in (args.run1, args.run2)]
    fixtures = json.loads((args.run1/'fixtures.json').read_text())
    chart_rows, report = [], ['| Case | HOSQ µs | Glaze µs | yyjson µs | H / G | H / Y |',
                              '|---|---:|---:|---:|---:|---:|']
    for f in fixtures:
        case = f['case']
        times = {e: statistics.mean(r[(case, e)]['median_us'] for r in runs)
                 for e in ('HOSQ', 'Glaze', 'yyjson')}
        hg = statistics.mean(r[(case, 'HOSQ')]['median_us']/r[(case, 'Glaze')]['median_us'] for r in runs)
        hy = statistics.mean(r[(case, 'HOSQ')]['median_us']/r[(case, 'yyjson')]['median_us'] for r in runs)
        report.append(f"| {case} | {times['HOSQ']:.3f} | {times['Glaze']:.3f} | {times['yyjson']:.3f} | {hg:.3f} | {hy:.3f} |")
        for e in ('HOSQ', 'Glaze', 'yyjson'):
            ratios = [r[(case, e)]['median_us']/r[(case, 'yyjson')]['median_us'] for r in runs]
            chart_rows.append(dict(case=case, engine=e, relative_to_yyjson=statistics.mean(ratios),
                                   ratio_run1=ratios[0], ratio_run2=ratios[1],
                                   ratio_min=min(ratios), ratio_max=max(ratios),
                                   cpu_us=times[e], cpu_us_run1=runs[0][(case,e)]['median_us'],
                                   cpu_us_run2=runs[1][(case,e)]['median_us'],
                                   input_bytes=f['bytes'], rows=f['rows'],
                                   selected_paths=', '.join(f['selected_paths'])))
    controls = []
    for case in ('medium/all','large/all'):
        if all((case,'HOSQ-before') in r for r in runs):
            for i,r in enumerate(runs,1):
                before=r[(case,'HOSQ-before')]['median_us'];after=r[(case,'HOSQ')]['median_us'];glaze=r[(case,'Glaze')]['median_us']
                controls.append(dict(case=case,run=i,before_us=before,after_us=after,glaze_us=glaze,
                                     after_over_before=after/before,before_over_glaze=before/glaze,
                                     after_over_glaze=after/glaze))
    (args.output/'chart-data.json').write_text(json.dumps(chart_rows,indent=2)+'\n')
    (args.output/'before-control.json').write_text(json.dumps(controls,indent=2)+'\n')
    (args.output/'table.md').write_text('\n'.join(report)+'\n')
    print('\n'.join(report))
    print(json.dumps(controls,indent=2))


if __name__ == '__main__':
    main()
