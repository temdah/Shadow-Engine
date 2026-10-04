"""Keep every explicit policy target strict; reject mixed configurations."""
import unittest
import itertools

import validate_refactor as refactor

TARGETS = (refactor.V120_POLICY_TARGET, refactor.V200_POLICY_TARGET,
           refactor.V30_B4_16_POLICY_TARGET, refactor.V30_B4_21_POLICY_TARGET)


class CapacityComparisonTests(unittest.TestCase):
    def test_all_exact_targets_pass(self):
        for target in TARGETS:
            refactor.validate_policy_constants(dict(target), target)

    def test_each_mixed_constant_is_rejected_in_all_directions(self):
        for wanted, other in itertools.permutations(TARGETS, 2):
            for name in wanted:
                if wanted[name] == other[name]:
                    continue
                with self.subTest(target=wanted['TOTAL_LOCAL_MAPS'], field=name):
                    mixed = {**wanted, name: other[name]}
                    with self.assertRaises(AssertionError):
                        refactor.validate_policy_constants(mixed, wanted)

    def test_missing_constant_is_rejected(self):
        for target in TARGETS:
            for name in refactor.POLICY_CONSTANTS:
                missing = dict(target)
                del missing[name]
                with self.subTest(field=name), self.assertRaises(AssertionError):
                    refactor.validate_policy_constants(missing, target)

    def test_original_geometry_and_pass_envelope(self):
        values = {key: int(value.rstrip('U'), 0)
                  for key, value in refactor.V120_POLICY_TARGET.items()}
        self.assertEqual(values['TOTAL_LOCAL_MAPS'], 24)
        self.assertEqual(values['TARGET_DYNAMIC_B4'], 16)
        self.assertEqual(values['TARGET_CACHE_A8'], 4)
        self.assertEqual(values['RENDER_QUEUE_FIRST_MAP_OFFSET'], 16 + 25 * 0x24C0)
        self.assertEqual(values['RENDER_QUEUE_SECOND_MAP_OFFSET'],
                         values['RENDER_QUEUE_FIRST_MAP_OFFSET'] + 25 * 4)
        self.assertEqual(values['RENDER_QUEUE_COUNT_OFFSET'],
                         values['RENDER_QUEUE_SECOND_MAP_OFFSET'] + 25 * 4)
        self.assertEqual(values['REQUIRED_PASS_TABLE_SLOTS'], (24 + 1) * 2)
        self.assertEqual(values['REQUIRED_PASS_MAX_KEY'], (24 << 9) | 0x10C)

    def test_30_map_isolation_differs_only_in_admission_from_v200(self):
        difference = {name for name in refactor.V200_POLICY_TARGET
                      if refactor.V200_POLICY_TARGET[name] != refactor.V30_B4_16_POLICY_TARGET[name]}
        self.assertEqual(difference, {'TARGET_DYNAMIC_B4'})
        self.assertEqual(refactor.V30_B4_16_POLICY_TARGET['TARGET_DYNAMIC_B4'], '16U')

    def test_intermediate_target_changes_only_b4_from_low_admission(self):
        difference = {name for name in refactor.V30_B4_16_POLICY_TARGET
                      if refactor.V30_B4_16_POLICY_TARGET[name] != refactor.V30_B4_21_POLICY_TARGET[name]}
        self.assertEqual(difference, {'TARGET_DYNAMIC_B4'})
        self.assertEqual(refactor.V30_B4_21_POLICY_TARGET['TARGET_DYNAMIC_B4'], '21U')


if __name__ == '__main__':
    unittest.main()
