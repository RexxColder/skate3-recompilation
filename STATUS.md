# Skate 3 Recompilation - Estado Actual

## Estado: DECODING ENGINE LOGIC (Phase D.3/D.4 RESOLVED)

### Resoluciones Recientes (Phase D.3/D.4):
- ✅ **FIXED: sub_826B57D0 Crash**: Identificado error de cálculo en base address (`lis -31997`). El puntero se almacenaba en `0x830186B0` pero el juego leía de `0x830286B0`. Corregido en `gpu_hooks.cpp`.
- ✅ **FIXED: sub_82A60B90 VTable Crash**: Implementada interfaz D3D dummy y VTable con stubs para `Allocate`, `AddRef` y `Release`.
- ✅ **Critical Sections**: Inicializadas en dev+72 y dev+112.
- ✅ **Memset Monitoring**: Suprimidos memsets a 0x0 menores a 1KB; traza limpia de fallos de memoria primarios.

### Lo que funciona:
- Runtime ReXGlue y Vulkan GPU (Radeon RX 580) estables.
- Swapchains (1280x720) activas.
- **D3D Initialization Complete**: El motor ya no crashea durante la configuración del dispositivo.
- **Custom Memory Allocation**: El juego ahora puede usar nuestro stub de `Allocate` para sus buffers internos.

### Lo que falta (Próximos pasos):
- ❌ **Deadlock en Iteración Principal**: Tras inicializar el dispositivo, el thread principal parece entrar en un wait infinito o halt silencioso.
- 1. Volver a investigar con **GDB** el punto exacto del freeze tras superar `sub_82A60B90`.
- 2. Implementar más stubs en la VTable si detectamos más llamadas indirectas faltantes.

### Archivos clave:
- `src/gpu_hooks.h` / `.cpp`: Sistema de interceptación con 10 hooks activos.
- `generated/skate3_recomp.43.cpp`: Contiene `sub_8293F460` (caller principal).
- `generated/skate3_recomp.51.cpp`: Contiene `sub_82A60208` y `sub_82A60600`.
