from __future__ import annotations

from functools import partial
from itertools import chain
from typing import List, Tuple
from warnings import warn

import numpy as np
import numba as nb

from htm.bindings.sdr import SDR


# class SDR(SDR):
#     def __init__(self, *args, **kwargs):
#         super().__init__(*args, **kwargs)
#
#     def permute(self, axes: tuple[int]) -> SDR:
#         if len(axes) != len(self.dimensions):
#             raise ValueError(fr"This SDR is {len(self.dimensions)} dimensional, but {len(axes)} dimensions were given.")
#
#         dummy = self.dense.copy()
#         dummy = np.transpose(dummy, axes)
#
#         self.reshape(dummy.shape)
#         self.dense = dummy
#         return self
#
#     def transpose(self, axes: tuple[int]) -> SDR:
#         return self.permute(axes)
#
#     def squeeze(self) -> SDR:
#         new_shape = list(filter(lambda x: x > 1, self.dimensions))
#         return self.reshape(new_shape)
#
#     def flatten(self) -> SDR:
#         new_shape = flatten_shape(self.dimensions)
#         return self.reshape(new_shape)
#
#     @property
#     def shape(self) -> tuple[int]:
#         return tuple(self.dimensions)


def sdr_max_pool(input_sdr: SDR, ratio: int, axis: int = 0) -> SDR:
    """ Performs max-pooling on the given SDR, on `axis` with `ratio` """
    if ratio > 1:
        dims = input_sdr.dimensions
        d, m = divmod(dims[axis], ratio)

        if m:
            raise AssertionError(f"Max pooling dimension must be divisible by ratio, got: {dims[axis], ratio}")

        dims[axis] = d
        new_coords = np.array(input_sdr.coordinates)
        new_coords[axis] //= ratio

        new_sdr = SDR(dims)
        new_sdr.coordinates = new_coords

        return new_sdr
    else:
        return input_sdr


def sdr_merge(*inputs, mode: str, axis: int = 0) -> SDR:
    """ Merges input SDRs with given mode (axis only relevant for concatenation)

        inputs: Any number of SDRs to merge (2 or more)

        mode:
            - `u`: Union
            - `i`: Intersection
            - `sd` or `xor`: Symmetric difference
            - `c`: Concatenation
            - `s`: Stacking. Creates new 0th axis.
            - `nos`: Non-overlapping sum. All bits that are unique to a single sdr

        axis: (default 0) Axis of concatenation.
        """
    if len(inputs) == 1:
        return inputs[0]

    shape = _check_shapes(*inputs)

    # union
    if mode == 'u':
        return SDR(shape).union(inputs)

    # intersection
    elif mode == 'i':
        return SDR(shape).intersection(inputs)

    # symmetric difference
    elif mode == 'sd' or mode == 'xor':
        return _sdr_symmetric_diff(*inputs)

    # non overlapping sum (true symmetric diff)
    elif mode == 'nos':
        return _sdr_non_overlapping_sum(*inputs)

    # concat
    elif mode == 'c':
        return _sdr_concat(*inputs, axis=axis)

    # stack (creates new 0th axis)
    elif mode == 's':
        return _sdr_stack(*inputs)

    else:
        raise ValueError(f"Unknown SDR merger mode `{mode}`")


def _check_shapes(*inputs) -> np.ndarray:
    """ Returns dimension of first input, fails if not all input SDRs have the same dimensions """
    s0 = inputs[0].dimensions
    for idx, sdr in enumerate(inputs):
        if s0 != sdr.dimensions:
            raise RuntimeError(f"All SDRs must have the same dimensions, got {s0} for sdr_0 and {sdr.dimensions} for "
                               f"sdr_{idx}")
    return s0


def _sdr_symmetric_diff(*inputs) -> SDR:
    shape = inputs[0].dimensions
    return _sdr_subtract(SDR(shape).union(inputs), SDR(shape).intersection(inputs))


def _sdr_non_overlapping_sum(*inputs) -> SDR:
    calc_result = _sdr_non_overlapping_sum_impl(*[sdr.dense for sdr in inputs])
    new_sdr = SDR(calc_result.shape)
    new_sdr.dense = calc_result
    return new_sdr


def _sdr_concat(*inputs, axis: int) -> SDR:
    new_shape = concat_shapes(*[x.dimensions for x in inputs], axis=axis)
    return SDR(new_shape).concatenate(inputs, axis=axis)


def _sdr_stack(*inputs) -> SDR:
    new_dim = [len(inputs), *inputs[0].dimensions]
    return _sdr_concat(*inputs, axis=0).reshape(new_dim)


def _sdr_subtract(sdr_1: SDR, sdr_2: SDR) -> SDR:
    """ Subtract on-bits of `sdr_2` from `sdr_1` using set diff """
    if sdr_1.dimensions != sdr_2.dimensions:
        raise ValueError(f"When subtracting, both SDRs must have the same dimensions, got: `{sdr_1.dimensions}`,"
                         f" `{sdr_2.dimensions}`")

    new_sdr = SDR(sdr_1.dimensions)
    new_sdr.sparse = np.setdiff1d(sdr_1.sparse, sdr_2.sparse, assume_unique=True)  # get diff with sets

    return new_sdr


def concat_shapes(*shapes, axis=0) -> Tuple[int, ...]:
    """ Returns the would-be shape of an SDR, if concatenated SDRs with 'shapes' on 'axis' """

    if len(set(len(s) for s in shapes)) != 1:
        raise ValueError("All shapes must have same number of dimensions")

    ndim = len(shapes[0])

    if axis >= ndim:
        raise ValueError(f"Invalid axis {axis} for shapes with {ndim} dimensions")

    # handle case for negative axis
    axis = _positify_axis(axis, ndim)

    # check shapes per dimension
    for dim, _ in enumerate(shapes[0]):
        if dim == axis:
            continue
        if len(set(s[dim] for s in shapes)) != 1:
            raise ValueError(
                f"Shapes must be equal in all axes except the concatenated axis, got different shapes for axis {dim}")

    # create new shape
    new_shape = []
    for dim, size in enumerate(shapes[0]):
        size = sum(s[dim] for s in shapes) if dim == axis else size
        new_shape.append(size)

    return tuple(new_shape)


def _positify_axis(axis, ndim):
    if axis < 0:
        _old = axis
        axis = ndim + _old
        if axis < 0:
            raise ValueError(f"Invalid axis {_old} for shape with {ndim} dimensions")
    return axis


def flatten_shape(shape: np.ndarray | list[int, ...] | tuple[int, ...]) -> np.ndarray:
    """ Returns flattened shape, i.e. [2, 2, 32] --> [128] """
    return np.prod(shape, keepdims=True)


def sdr_flatten(input_sdr: SDR) -> SDR:
    new_shape = flatten_shape(input_sdr.dimensions)
    return input_sdr.reshape(new_shape)


def sdr_squeeze(input_sdr: SDR, axes: int | tuple[int, ...] | list[int] = None) -> SDR:
    ndim = len(input_sdr.dimensions)

    if axes is None:
        # if None -- squeeze all
        axes = range(ndim)
    else:
        # if int -- turn to tuple
        axes = (axes,) if isinstance(axes, int) else axes
        # handle any negative axes
        axes = set(map(partial(_positify_axis, ndim=ndim), axes))

    new_shape = list(map(lambda x: x[1],
                         filter(lambda x: x[1] > 1 or x[0] not in axes,
                                enumerate(input_sdr.dimensions))))

    return input_sdr.reshape(new_shape)


@nb.njit
def _sdr_non_overlapping_sum_impl(*inputs):
    temp = np.sum(np.stack(inputs), axis=0)
    ans = np.zeros_like(temp)
    ans[temp == 1] = 1
    return ans

# def squeeze_shape(shape: List[int] | Tuple[int]) -> Tuple[int, ...]:
#     """ Squeezes the shape by merging last 2 dims, i.e. [2, 2, 32] --> [2, 64]  """
#     if len(shape) < 2:
#         raise ValueError("Cannot squeeze shape with less than 2 dims")
#     else:
#         new_last = shape[-1] * shape[-2]
#         new_shape = [dim for dim in shape[:-2]]
#         new_shape.append(new_last)
#         return tuple(new_shape)


# def sdr_union(*inputs) -> SDR:
#     indices = np.array(list(set(chain(*(s.sparse for s in inputs)))))
#     new_sdr = SDR(inputs[0].dimensions)
#     new_sdr.sparse = indices
#     return new_sdr
