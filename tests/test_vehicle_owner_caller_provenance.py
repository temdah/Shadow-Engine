import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DIAGNOSTIC = (ROOT / "src/modules/18_vehicle_light_diagnostics.inc").read_text()
SHARED = (ROOT / "src/modules/00_shared_config_state.inc").read_text()


class VehicleOwnerCallerProvenanceTests(unittest.TestCase):
    def test_existing_hook_copies_immediate_native_caller(self):
        hook = DIAGNOSTIC.split("static void __fastcall hooked_vehicle_light_update")[1]
        hook = hook.split("static int install_vehicle_owner_observer")[0]
        self.assertEqual(hook.count("__builtin_return_address(0)"), 1)
        self.assertIn("force_update,update_caller", hook)
        self.assertIn("d->original_light_update(component,skeleton,world_transform,delta,force_update);", hook)

    def test_skeleton_header_is_bounded_and_capture_only(self):
        begin = DIAGNOSTIC.split("static int begin_vehicle_owner_record")[1]
        begin = begin.split("static void __fastcall hooked_vehicle_light_transform")[0]
        self.assertIn("readable_memory(skeleton,sizeof(void *))", begin)
        self.assertIn("r->skeleton_header=*(void **)skeleton", begin)
        self.assertIn("skeleton_header_valid=1U", begin)
        self.assertIn("void *skeleton_header;", SHARED)
        self.assertIn("uintptr_t update_caller;", SHARED)

    def test_worker_logs_only_copied_values(self):
        flush = DIAGNOSTIC.split("static DWORD WINAPI flush_vehicle_candidate_capture")[1]
        flush = flush.split("static void schedule_vehicle_candidate_capture_flush")[0]
        self.assertIn("skeletonHeader=%p", flush)
        self.assertIn("updateCaller=0x%llX", flush)
        self.assertIn("disruptBase=%p callerAndHeaderCopied=1", flush)
        self.assertNotIn("readable_memory", flush)
        self.assertNotIn("__builtin_return_address", flush)

    def test_only_authorized_batch_hook_no_wait_or_policy(self):
        self.assertEqual(DIAGNOSTIC.count("install_detour("), 3)
        self.assertNotIn("WaitFor", DIAGNOSTIC)
        self.assertNotIn("SetEvent", DIAGNOSTIC)


if __name__ == "__main__":
    unittest.main()
