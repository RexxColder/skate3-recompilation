# Progress

- Fixed fundamental engine bug crashing the indirect VTable call handler (`PPC_CALL_INDIRECT_FUNC`) by discovering native thunk address constraints within ReXGlue bounds.
- Re-assigned `g_guestMethodThunksBase` outside of the dynamically generated heap `0x30035000` to a static, completely safe reservation chunk inside exactly `0x82F80000`.
- Native `sub_82A60970` and `sub_82A60B90` initialization sequences successfully run natively and construct resource dictionaries unhindered.
- Surpassed `sub_82A5ED78`'s previous SIGSEGV entirely. The engine is progressing to deeper execution stages within `sub_826B57D0`.

# Next Steps
- Trace the deadlock occurring after Direct3D device creation.
- Trace the new crash discovered at `sub_829F2FB8`.
- Set up GDB debugging to analyze engine state during the infinite wait.
