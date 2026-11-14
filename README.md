# Spam Buff v1.8.0

**Control por teclado:** pulsa **B** durante un nivel para alternar entre **SPAM ON** y **SPAM OFF**. El botón flotante de pantalla fue eliminado.

## Configuración
- La tecla se puede cambiar desde los ajustes de Geode; la tecla predeterminada es **B**.
- Cada pulsación de B alterna el modo automático; mantener B presionada no lo repite.
- **Velocidad del spam:** ajustable entre **1 y 20 CPS** (clics completos por segundo).
- El modo automático alterna presionar y soltar usando un reloj monotónico, para mantener el ritmo aunque cambie la tasa de fotogramas y evitar ráfagas de recuperación después de pausas.
- **Precisión de toque:** cada pulsación y liberación física se programa en ticks de física consecutivos para que un toque muy corto no se pierda dentro del mismo paso de simulación.
- La detección de spam rápido sigue disponible y usa el mismo motor de velocidad configurable.
- Compatibilidad objetivo: Geometry Dash 2.2081, Geode 5.11.2 y Android ARM64.

## Compilación
El proyecto se compila mediante GitHub Actions para Android64 y publica el archivo .geode como artefacto.
