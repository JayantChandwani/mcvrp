from __future__ import annotations

from typing import Iterable, Sequence


METHOD_LABELS = {
    "cluster_first": "Cluster-First",
    "match_first": "Match-First",
    "ca_ilp": "CA (ILP)",
    "ca_mis": "CA (MIS)",
}

SCENARIO_LABELS = {
    "scenario1": "Scenario-1",
    "scenario2": "Scenario-2",
    "scenario3": "Scenario-3",
}

METHOD_COLORS = {
    "cluster_first": "#4C72B0",
    "match_first": "#DD8452",
    "ca_ilp": "#55A868",
    "ca_mis": "#C44E52",
}

METHOD_MARKERS = {
    "cluster_first": "o",
    "match_first": "s",
    "ca_ilp": "^",
    "ca_mis": "D",
}


def configure_matplotlib() -> None:
    import matplotlib as mpl

    mpl.rcParams.update({
        "font.family": "DejaVu Serif",
        "font.size": 11,
        "axes.titlesize": 14,
        "axes.labelsize": 12,
        "xtick.labelsize": 10,
        "ytick.labelsize": 10,
        "legend.fontsize": 10,
        "axes.linewidth": 0.8,
        "lines.linewidth": 2.0,
        "axes.grid": False,
        "figure.dpi": 170,
        "savefig.dpi": 170,
    })


def method_label(method: str) -> str:
    return METHOD_LABELS.get(method, method)


def scenario_label(scenario: str) -> str:
    return SCENARIO_LABELS.get(scenario, scenario)


def color_for_method(method: str) -> str:
    return METHOD_COLORS.get(method, "#666666")


def marker_for_method(method: str) -> str:
    return METHOD_MARKERS.get(method, "o")


def title(ax, text: str, pad: int = 4) -> None:
    ax.set_title(text, fontsize=14, pad=pad, fontweight="semibold")


def label_axes(ax, xlabel: str | None = None, ylabel: str | None = None) -> None:
    if xlabel is not None:
        ax.set_xlabel(xlabel)
    if ylabel is not None:
        ax.set_ylabel(ylabel)


def disable_grid(ax) -> None:
    ax.grid(False)


def colorize_boxplot(boxplot, keys: Sequence[str]) -> None:
    for patch, key in zip(boxplot["boxes"], keys):
        color = color_for_method(key)
        patch.set_facecolor(color)
        patch.set_edgecolor("#222222")
        patch.set_alpha(0.85)

    for element_name in ("whiskers", "caps", "medians", "means"):
        if element_name not in boxplot:
            continue
        for element in boxplot[element_name]:
            element.set_color("#222222")

    for mean in boxplot.get("means", []):
        mean.set_marker("^")
        mean.set_markerfacecolor("#111111")
        mean.set_markeredgecolor("#111111")
        mean.set_markersize(7)
