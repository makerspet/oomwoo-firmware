import unittest

from tools.check_isr_disassembly import symbol_instructions, verify_leaf


LEAF_DISASSEMBLY = """
080045d4 <(anonymous namespace)::hard_stop_isr(void*, oomwoo_cpu_stop_reason_t)>:
 80045d4: f04f 4390  mov.w r3, #1207959552
 80045d8: 619a       str r2, [r3, #24]
 80045da: 4770       bx lr

080045dc <next_symbol>:
 80045dc: 4770       bx lr
"""

CALLING_DISASSEMBLY = """
080045d4 <hard_stop_isr>:
 80045d4: f000 f812  bl 80045fc <digitalWrite>
 80045d8: 4770       bx lr
"""


class DisassemblyCheckTest(unittest.TestCase):
    def test_extracts_only_requested_symbol(self) -> None:
        self.assertEqual(
            symbol_instructions(LEAF_DISASSEMBLY, "hard_stop_isr"),
            ["mov.w", "str", "bx"],
        )

    def test_accepts_leaf_symbol(self) -> None:
        self.assertEqual(
            verify_leaf(LEAF_DISASSEMBLY, "hard_stop_isr"),
            ["mov.w", "str", "bx"],
        )

    def test_rejects_call_instruction(self) -> None:
        with self.assertRaisesRegex(ValueError, "contains call instructions"):
            verify_leaf(CALLING_DISASSEMBLY, "hard_stop_isr")

    def test_rejects_missing_symbol(self) -> None:
        with self.assertRaisesRegex(ValueError, "was not found"):
            verify_leaf(LEAF_DISASSEMBLY, "missing_isr")


if __name__ == "__main__":
    unittest.main()
