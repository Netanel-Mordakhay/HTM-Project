from __future__ import annotations

from typing import Tuple, Set
from collections import namedtuple

import numpy as np
import numba as nb
from htm.bindings.sdr import SDR
from scipy import stats
from matplotlib import pyplot as plt

from htm_source.utils.general import get_slices, smooth_predictions

Slice = namedtuple('Slice', ['start', 'end'])


def calc_conf_mat(preds: Set[Slice], targets: Set[Slice], data_len: int) -> Tuple[int, int, int, int, int]:
    """
    Calculate confusion matrix values for given predictions and labels.
    Both predictions and labels (targets) are supplied as a set of slices, representing windows.

    returns: tp, fp, fn, tn, hit_targets_area
    """

    tp = 0
    hits = set()
    was_hit = set()

    for tag in targets:
        # for each label window, mark all related (intersecting) predictions as hits (counts as 1 hit per tag)
        if related := set(filter(lambda x: (tag.start <= x.start < tag.end) or (tag.start <= x.end < tag.end), preds)):
            hits.update(related)
            was_hit.add(tag)
            tp += 1

    hit_targets_area = sum([tag.end - tag.start for tag in was_hit])

    # fp are all predictions that didn't hit at least one label
    fp = len(preds.difference(hits))

    # fn are all tags that were never hit
    fn = len(targets.difference(was_hit))

    tn = data_len - tp - fp - fn

    return tp, fp, fn, tn, hit_targets_area


def _convert_arr_to_slices(array: np.ndarray, window: int, learn_period: int, on_change=False) -> Tuple[
    np.ndarray, Set[Slice]]:
    """ Convert an array of (predictions/labels) to an array of 1s, merging with given window size.
        In case of non-spike input, use `on_change=True`

        returns both the new array, and the slices as a set """

    if on_change:
        new_array = np.zeros_like(array)
        new_array[1:] = np.abs(array[1:] - array[:-1])
    else:
        new_array = array.copy()

    # disregard learning period
    new_array[:learn_period] = 0

    # close gaps by correlating with box filter and taking the ceil
    new_array = (np.correlate(new_array, np.ones(window * 2 + 1), mode='same') > 0).astype(np.int32)

    # turn into slices
    slices = get_slices(new_array)
    slices = set(map(lambda t: Slice(start=t[0], end=t[1]), slices))

    return new_array, slices


@nb.jit(nopython=True, nogil=True, cache=True)
def _calc_pr_re_fb(tp: int, fp: int, fn: int, beta: float = 1.) -> Tuple[float, float, float]:
    """ Calculates and returns precision, recall and Fbeta score from input confusion matrix values """
    if tp == fp == fn == 0:
        return 1., 1., 1.

    b2 = beta ** 2
    pr, re = _calc_precision_recall(tp, fp, fn)
    fb = ((1 + b2) * tp) / max((1 + b2) * tp + b2 * fn + fp, 1.)
    return pr, re, fb


@nb.jit(nopython=True, nogil=True, cache=True)
def _calc_iou(predictions: np.ndarray, targets: np.ndarray) -> float:
    intersection = np.logical_and(predictions, targets)
    union = np.logical_or(predictions, targets)
    return np.count_nonzero(intersection) / max(np.count_nonzero(union), 1.)


@nb.jit(nopython=True, nogil=True, cache=True)
def _calc_precision_recall(tp: int, fp: int, fn: int) -> Tuple[float, float]:
    pr = tp / max(tp + fp, 1)
    re = tp / max(tp + fn, 1)
    return pr, re


@nb.jit(nopython=True, nogil=True, cache=True)
def _calc_harmonic_mean_3(x: float | int, y: float | int, z: float | int, /) -> float:
    return (3 * x * y * z) / max(x * y + x * z + y * z, 1.)


def calc_metrics(*,
                 predictions: np.ndarray,
                 targets_merged: np.ndarray,
                 target_windows: set[Slice],
                 thresh: float,
                 beta: float,
                 grace_window: int,
                 data_len: int,
                 learn_period: int) -> dict[str, float]:
    pred_spikes = (predictions > thresh).astype(np.int32)
    predictions_merged, pred_windows = _convert_arr_to_slices(pred_spikes, grace_window, on_change=False,
                                                              learn_period=learn_period)
    tp, fp, fn, tn, hit_area = calc_conf_mat(pred_windows, target_windows, data_len)
    pr, re, fb = _calc_pr_re_fb(tp, fp, fn, beta)
    accuracy = (tp + tn) / (tp + tn + fp + fn)
    hou = hit_area / max(np.count_nonzero(np.logical_or(targets_merged, predictions_merged)), 1)
    iol = np.count_nonzero(np.logical_and(targets_merged, predictions_merged)) / np.count_nonzero(targets_merged)
    htm_score = stats.hmean([pr, re, hou, iol])
    return {'precision': pr, 'recall': re, f'F{beta}': fb, 'hou': hou, 'iol': iol, 'htm_score': htm_score, 'accuracy':accuracy}


def find_best_score(predictions: np.ndarray,
                    targets: np.ndarray,
                    *,
                    optimize: str,
                    thresholds: np.ndarray,
                    smoothing_window_sizes: np.ndarray,
                    learn_period: int,
                    grace_window: int,
                    thresh_delta: float,
                    on_change: bool = False,
                    beta: float = 1.0) -> Tuple[float, dict, dict]:
    """
    Find the highest score according to what is being optimized,
    performing a grid search on the different thresholds and label window sizes...

    Returns: (score, fbeta, precision, recall, iou), (threshold, box_size)
    """

    best = 0
    best_metrics = {f'F{beta}': 0, 'precision': 0, 'recall': 0, 'hou': 0, 'iol': 0}
    best_params = {'thresh': 0, 'box_size': 0}
    data_len = len(predictions)
    targets_merged, target_windows = _convert_arr_to_slices(targets, grace_window, learn_period=learn_period,
                                                            on_change=on_change)
    for box_size in smoothing_window_sizes:
        smooth_preds = smooth_predictions(predictions, box_size)
        for thresh in thresholds:
            # choose threshold based on mean of high and low (thresh +- delta)
            low_thresh_metrics = calc_metrics(predictions=smooth_preds, target_windows=target_windows, beta=beta,
                                              data_len=data_len, grace_window=grace_window, learn_period=learn_period,
                                              targets_merged=targets_merged, thresh=thresh - thresh_delta)

            high_thresh_metrics = calc_metrics(predictions=smooth_preds, target_windows=target_windows, beta=beta,
                                               data_len=data_len, grace_window=grace_window, learn_period=learn_period,
                                               targets_merged=targets_merged, thresh=thresh + thresh_delta)

            mid_score = (low_thresh_metrics[optimize] + high_thresh_metrics[optimize]) / 2.

            if mid_score > best:
                best = mid_score
                best_params['thresh'] = float(thresh)
                best_params['box_size'] = int(box_size)

    if best > 0:
        best_predictions = smooth_predictions(predictions, best_params['box_size'])
        best_metrics = calc_metrics(predictions=best_predictions, target_windows=target_windows, beta=beta,
                                    data_len=data_len, grace_window=grace_window, learn_period=learn_period,
                                    targets_merged=targets_merged, thresh=best_params['thresh'])
        best = best_metrics.pop(optimize)

        print(
            f"Found best {optimize}: {best:.4f} (" +
            ', '.join([f"{k}: {v:.2f}" for k, v in best_metrics.items()]) + ") with (" +
            ', '.join([f"{k}: {v:.2f}" for k, v in best_params.items()]) + ")")
    else:
        print("Best score is 0...")

    return best, best_metrics, best_params


def merge_targets_and_predictions(predictions: np.ndarray,
                                  targets: np.ndarray,
                                  *,
                                  thresh: float,
                                  smooth_box_size: int,
                                  grace_window: int,
                                  learn_period: int) -> Tuple[np.ndarray, np.ndarray]:
    targets, _ = _convert_arr_to_slices(targets, grace_window, learn_period=learn_period)
    predictions_smooth = smooth_predictions(predictions, smooth_box_size)
    pred_spikes = (predictions_smooth > thresh).astype(np.int32)
    predictions, _ = _convert_arr_to_slices(pred_spikes, grace_window, on_change=False, learn_period=learn_period)
    return predictions, targets


def plot_windows(pred_merged: np.ndarray, target_merged: np.ndarray, pred_orig: np.ndarray, ground_truth: np.ndarray,
                 thresh: float, learn_period: int):
    learn_period = np.ones(learn_period) * 0.5
    pred_raw = np.zeros_like(pred_orig)
    pred_raw[pred_orig > thresh] = pred_orig[pred_orig > thresh]

    plt.figure(figsize=(15, 5))
    plt.plot(target_merged, label="Anomaly Windows")
    plt.plot(ground_truth, label="Ground Truth")
    plt.plot(learn_period, color='r', label='Learning Period')
    plt.title("Windows around GT")
    plt.legend()
    plt.show()

    plt.figure(figsize=(15, 5))
    plt.plot(target_merged, label="Anomaly Windows")
    plt.plot(pred_merged, label="Anomaly Predictions")
    plt.plot(learn_period, color='r', label='Learning Period')
    plt.title("Final Predictions")
    plt.legend()
    plt.show()


def calc_anomaly_score(active: SDR, predictive: SDR) -> float:
    """ Implementation copied from htm.core """

    if len(active.sparse) == 0:
        return 0.

    intersect_sdr = SDR(active.dimensions).intersection(active, predictive)
    intersect_num = len(intersect_sdr.sparse)
    active_num = len(active.sparse)

    return 1. - (intersect_num / active_num)
