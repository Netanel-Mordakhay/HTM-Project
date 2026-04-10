from __future__ import annotations

import re
from typing import Tuple

import networkx as nx

htm_name_pattern = re.compile(r'(?P<layer>^[Ll]\d+_)(?P<name>[\w-]+$)')


def build_and_validate_graph(feature_plan: dict, connection_plan: dict) -> nx.DiGraph:

    graph = nx.DiGraph()
    layer_indices = set()
    # build graph and ensure all names match regex and all layers point forward
    for target, inputs in connection_plan.items():
        t_idx, _ = get_layer_and_name(target, htm_name_pattern)

        layer_indices.add(t_idx)
        graph.add_node(target, layer=t_idx)

        for inp in inputs:
            in_idx, _ = get_layer_and_name(inp, htm_name_pattern)
            if not in_idx < t_idx:
                raise ValueError(f"Layer points backwards: {target} <-- {inp}")

            layer_indices.add(in_idx)
            graph.add_node(inp, layer=in_idx)
            graph.add_edge(inp, target)

    # check layer idx are consecutive, 0...n
    if bad_idx := set(range(len(layer_indices))).symmetric_difference(layer_indices):
        raise ValueError(f"Layer indexing error, these layers are either missing or should no exist: {bad_idx}")

    # ensure DAG
    if not nx.is_directed_acyclic_graph(graph):
        raise ValueError(f"Graph is not a DAG")

    # make sure only 1 component
    if len(components := list(nx.weakly_connected_components(graph))) != 1:
        raise ValueError(f"Graph is not connected properly, got the following components:\n{components}")

    heads = []
    sources = []
    # get source and head nodes
    for node in graph.nodes():
        if graph.in_degree[node] == 0:
            sources.append(node)

        if graph.out_degree[node] == 0:
            heads.append(node)

    if not set(sources) == set(feature_plan.keys()):
        raise ValueError(f"Graph is not connected properly; either some leafs are not feature nodes"
                         f" or some feature nodes are not leafs")

    if len(heads) != 1:
        raise ValueError(f"Pyramid should have exactly 1 head, got: {heads}")

    return graph


def get_layer_dict(graph: nx.DiGraph) -> dict[int, tuple[str]]:
    layer_dict = dict()
    for node in nx.topological_sort(graph):
        layer_idx = graph.nodes[node]['layer']
        layer = layer_dict.get(layer_idx, [])
        layer.append(node)
        layer_dict[layer_idx] = layer

    assert len(layer_dict) == len(nx.dag_longest_path(graph)), "Layer dict and graph not the same depth"

    # make layers immutable
    for key, val in layer_dict.items():
        layer_dict[key] = tuple(val)

    return layer_dict


def get_layer_and_name(string: str, pattern: re.Pattern) -> Tuple[int, str]:
    # Checking if the match is successful
    if not (match := re.fullmatch(pattern, string)):
        raise ValueError(f"HTM id string must be of the pattern: `L<layer_idx>_<htm_name>`, got: {string}")

    layer = match.group('layer')
    name = match.group('name')

    return int(layer[1:-1]), name
