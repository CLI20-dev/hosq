#!/usr/bin/env python3
"""Plot paired-run ratios; whiskers are the range of run medians, not a CI."""
import argparse
import json
from pathlib import Path
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def medians(path):
    values = {}
    units = {'ns': 1, 'us': 1000, 'ms': 1000000, 's': 1000000000}
    for row in json.loads(path.read_text())['benchmarks']:
        if row.get('run_type') == 'iteration':
            values.setdefault(row['run_name'], []).append(row['cpu_time'] * units[row['time_unit']])
    return {key: statistics.median(value) for key, value in values.items()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run1', type=Path)
    parser.add_argument('run2', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    runs = [medians(args.run1), medians(args.run2)]
    panels = [
        ('Sparse extraction — typed selection', [
            ('small/sparse', 'Small · 109 B'), ('medium/sparse', 'Medium · 230 KB'),
            ('large/sparse', 'Large · 30 MB'), ('wide_first/sparse', 'Wide · id first'),
            ('wide_last/sparse', 'Wide · id last')]),
        ('All values — HOSQ visitor / yyjson & Glaze DOM', [
            ('small/all', 'Small · 109 B'), ('medium/all', 'Medium · 230 KB'),
            ('large/all', 'Large · 30 MB'), ('deep/all', 'Deep containers'),
            ('numbers/all', 'Mixed numbers'), ('strings_utf8/all', 'Long UTF-8 strings'),
            ('strings_escaped/all', 'Same strings · escaped')])]
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                         'axes.spines.top': False, 'axes.spines.right': False,
                         'axes.spines.left': False})
    fig, axes = plt.subplots(2, 1, figsize=(12, 11), gridspec_kw={'height_ratios': [5, 7]})
    colors = {'yyjson': '#8b97a5', 'HOSQ': '#1676bd', 'Glaze': '#8954bb'}
    for ax, (title, cases) in zip(axes, panels):
        limit = 1
        for j, engine in enumerate(['yyjson', 'HOSQ', 'Glaze']):
            center, lo, hi = [], [], []
            for case, label in cases:
                ratios = [r[case + '/' + engine] / r[case + '/yyjson'] for r in runs]
                mid = statistics.mean(ratios)
                center.append(mid); lo.append(mid - min(ratios)); hi.append(max(ratios) - mid)
                limit = max(limit, max(ratios))
            positions = np.arange(len(cases)) + (j - 1) * .23
            ax.barh(positions, center, height=.2, color=colors[engine], label=engine,
                    xerr=np.array([lo, hi]), error_kw={'elinewidth': 1, 'capsize': 2, 'ecolor': '#333333'})
            for y, value, upper in zip(positions, center, hi):
                ax.annotate(f'{value:.2f}×', (value + upper, y), xytext=(5, 0),
                            textcoords='offset points', va='center', fontsize=9, color=colors[engine])
        ax.set_yticks(range(len(cases)), [label for _, label in cases])
        ax.invert_yaxis()
        ax.set_xlim(0, limit * 1.15 + .15)
        ax.axvline(1, linestyle='--', linewidth=1, color='#555555', zorder=0)
        ax.grid(axis='x', alpha=.15); ax.set_axisbelow(True)
        ax.tick_params(axis='y', length=0, pad=10)
        ax.set_title(title, loc='left', fontsize=13, fontweight='bold', pad=15)
        ax.set_xlabel('CPU time relative to yyjson (lower is faster)', fontsize=10)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='upper right', bbox_to_anchor=(.97, .96), ncol=3, frameon=False)
    fig.suptitle('HOSQ / yyjson / Glaze', x=.04, y=.995, ha='left', fontsize=23, fontweight='bold')
    fig.text(.04, .965, 'Same decoded output · fresh process per case / implementation · yyjson = 1', fontsize=11)
    fig.text(.04, .025, 'Bars: mean of two within-run median ratios. Whiskers: run-to-run range (not confidence intervals).\n'
             'Glaze: typed id selection; generic_u64 DOM for dynamic full reads.\n'
             'Parsing, output consumption and output allocation/free included. Current power conditions; no historical timings.',
             fontsize=9, color='#444444', linespacing=1.6)
    fig.subplots_adjust(left=.24, right=.96, top=.91, bottom=.10, hspace=.58)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output.with_suffix('.png'), dpi=170, facecolor='white')
    svg = args.output.with_suffix('.svg')
    fig.savefig(svg, facecolor='white')
    svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')


if __name__ == '__main__':
    main()
