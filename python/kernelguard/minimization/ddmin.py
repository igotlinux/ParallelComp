"""
Delta Debugging (ddmin) Failure Minimization Engine
"""

from typing import Callable, List, TypeVar

T = TypeVar("T")


def ddmin(input_list: List[T], test_func: Callable[[List[T]], bool]) -> List[T]:
    """
    Zeller's Delta Debugging (ddmin) algorithm.
    Finds a 1-minimal subset of input_list that satisfies test_func(subset) == True.
    """
    if not input_list:
        return []

    # Verify that full input indeed triggers failure
    if not test_func(input_list):
        return input_list

    n = 2
    current = input_list

    while len(current) >= 2:
        subsets = []
        complements = []
        k = len(current) // n
        remainder = len(current) % n

        # Split into n chunks
        start = 0
        for i in range(n):
            chunk_size = k + (1 if i < remainder else 0)
            end = start + chunk_size
            subsets.append(current[start:end])
            complements.append(current[:start] + current[end:])
            start = end

        reduced = False

        # 1. Test subsets
        for sub in subsets:
            if sub and test_func(sub):
                current = sub
                n = max(n - 1, 2)
                reduced = True
                break

        if reduced:
            continue

        # 2. Test complements
        for comp in complements:
            if comp and test_func(comp):
                current = comp
                n = max(n - 1, 2)
                reduced = True
                break

        if reduced:
            continue

        # 3. Increase granularity
        if n == len(current):
            break
        n = min(n * 2, len(current))

    return current
