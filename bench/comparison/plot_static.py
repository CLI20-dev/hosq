#!/usr/bin/env python3
"""Export the typed benchmark figure; whiskers are ranges of two run ratios."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('data', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = {(r['case'], r['engine']): r for r in json.loads(args.data.read_text())}
    panels = [('Selected fields + skip', [
        ('small/id', 'Small / id · 109 B'), ('medium/id', 'Medium / id · 230 KB'),
        ('medium/many', 'Medium / 5 fields'), ('large/id', 'Large / id · 30 MB'),
        ('wide_first/id', 'Wide / id first'), ('wide_last/id', 'Wide / id last')]),
        ('All fields · typed schemas', [
        ('small/all', 'Small / all · 109 B'), ('medium/all', 'Medium / all · 230 KB'),
        ('large/all', 'Large / all · 30 MB'), ('numbers/all', 'Mixed numeric tuple'),
        ('strings_utf8/all', 'Long UTF-8 strings'), ('strings_escaped/all', 'Same strings / escaped')])]
    colors = {'HOSQ': '#2469A0', 'Glaze': '#8256A3', 'yyjson': '#86909C'}
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                         'axes.spines.top': False, 'axes.spines.right': False})
    fig, axes = plt.subplots(2, 1, figsize=(11.5, 12.5))
    limit = max(r['ratio_max'] for r in data.values()) * 1.16 + .12
    for ax, (title, cases) in zip(axes, panels):
        for j, engine in enumerate(('HOSQ','Glaze','yyjson')):
            rows = [data[(case, engine)] for case, _ in cases]
            v = np.array([r['relative_to_yyjson'] for r in rows])
            lo = v - np.array([r['ratio_min'] for r in rows])
            hi = np.array([r['ratio_max'] for r in rows]) - v
            y = np.arange(len(cases)) + (j-1)*.24
            ax.barh(y, v, height=.21, label=engine, color=colors[engine],
                    edgecolor='#263442', linewidth=.35,
                    xerr=np.array([lo,hi]), error_kw=dict(ecolor='#263442',capsize=2,elinewidth=.8))
            for yy, value, err in zip(y, v, hi):
                ax.annotate(f'{value:.2f}×', (value+err, yy), xytext=(5,0),
                            textcoords='offset points', va='center', fontsize=9)
        ax.set_yticks(range(len(cases)), [label for _, label in cases])
        ax.invert_yaxis(); ax.set_xlim(0,limit); ax.set_axisbelow(True)
        ax.grid(axis='x',alpha=.17); ax.axvline(1,color='#263442',lw=.8,ls='--')
        ax.set_title(title,loc='left',fontweight='bold',pad=13)
        ax.set_xlabel('CPU time relative to yyjson = 1 · lower is faster')
    fig.suptitle('HOSQ / Glaze / yyjson — typed JSON decoding',x=.03,ha='left',fontsize=17,fontweight='bold')
    fig.text(.03,.944,'Fresh selected-value vectors · parse + postpass + destruction · valid inputs',fontsize=10)
    fig.legend(*axes[0].get_legend_handles_labels(),loc='upper right',bbox_to_anchor=(.985,.986),frameon=False,ncol=3)
    fig.text(.03,.024,'Bars: mean of two independently ordered run ratios (5 repetitions each).\nWhiskers: range of the two run ratios, not confidence intervals. HOSQ full schemas use declared key order.\nGlaze 8.3.0 / yyjson 0.12.0 · Clang 21 -O3 · AC power · raw results: static-threeway-2026-09-08',fontsize=9,color='#485563')
    fig.subplots_adjust(left=.27,right=.97,top=.90,bottom=.105,hspace=.29)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    fig.savefig(args.output.with_suffix('.png'),dpi=180)
    fig.savefig(args.output.with_suffix('.svg'))
    plt.close(fig)


if __name__ == '__main__':
    main()
