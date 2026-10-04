"""Source safety contracts; native recorder semantics live in intersection_harness.c."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class IntersectionContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.observer = (ROOT / 'src/modules/17_intersection_diagnostics.inc').read_text()
        cls.boot = (ROOT / 'src/modules/70_bootstrap_orchestration.inc').read_text()
        cls.state = (ROOT / 'src/modules/00_shared_config_state.inc').read_text()

    def test_no_engine_ownership_or_waits(self):
        for forbidden in ('original_resource_wrapper', 'consume_external_slice_results(',
                          'Sleep(', 'WaitFor', 'reset_external_slice_cycle(',
                          'VirtualQuery(', 'while('):
            self.assertNotIn(forbidden, self.observer)

    def test_off_gate_precedes_record_access(self):
        body = self.observer.split('static void intersection_observe(', 1)[1]
        body = body.split('static void intersection_builder_enter', 1)[0]
        self.assertLess(body.index('&d->state'), body.index('find_existing_lifecycle_record_fast'))
        self.assertLess(body.index('kind==IX_COMPLETION'), body.index('record+offsets[i]'))

    def test_worker_uses_copies_only(self):
        body = self.observer.split('static DWORD WINAPI intersection_flush(', 1)[1]
        body = body.split('static void intersection_schedule_flush', 1)[0]
        for forbidden in ('readable_memory', 'ENGINE_ADDRESS', 'find_existing_lifecycle_record',
                          'e->record+', 'original_'):
            self.assertNotIn(forbidden, body)
        self.assertIn('d->events[', body)
        self.assertIn('d->anomalies[', body)

    def test_bounded_ring_and_explicit_retry(self):
        for definition in ('INTERSECTION_EVENTS 8192U', 'INTERSECTION_ANOMALIES 128U',
                           'INTERSECTION_RECORDS 256U', 'INTERSECTION_TIMEOUT_MS 60000U'):
            self.assertIn(definition, self.state)
        self.assertIn('if(flush) intersection_schedule_flush();', self.observer)
        self.assertIn('d->state==3 && (commands&4)', self.observer)

    def test_preflight_before_early_commit_and_deferred_write(self):
        preparation = self.boot.split('static int commit_early_patch_plan', 1)[0]
        self.assertIn('validate_intersection_sites()', preparation)
        deferred = self.boot.split('static int install_deferred_downstream', 1)[1]
        self.assertLess(deferred.index('install_intersection_observer()'),
                        deferred.index('install_frame_graph_tail_relay()'))
        self.assertRegex(deferred,
            r'if\(rollback\.failed_restores==0\) \{\s*'
            r'#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS\s*'
            r'rollback_intersection_observer\(\);\s*#endif')

    def test_regional_layout_stays_profile_data(self):
        self.assertIn('p->intersection_record_modrm', self.observer)
        self.assertIn('p->intersection_lookup_prologue', self.observer)
        self.assertNotIn('runtime_profile_id()', self.observer)
        self.assertIn('TlsSetValue(d->tls_index,scope->previous)', self.observer)

    def test_detour_original_is_published_before_entry_write(self):
        source = (ROOT / 'src/modules/10_runtime_primitives.inc').read_text()
        body = source.split('static int install_detour(',1)[1].split('static int install_inline_relay',1)[0]
        self.assertLess(body.index('InterlockedExchangePointer'),body.index('patch_write_bytes'))


if __name__ == '__main__':
    unittest.main()
