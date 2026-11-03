# DesktopAudioBridge
VST3 plugin (JUCE, Windows x64, GPLv3) that captures a Windows output via WASAPI loopback and sums it into the DAW output. / Plugin VST3 que captura uma saída do Windows via loopback WASAPI e soma na saída da DAW.

- [English](#english)
- [Português](#português)

---

## English

### Use case

You play guitar in Reaper through ASIO4ALL (minimum latency) and still want to hear Windows audio (browser, music, calls) on the same headphones/speakers. ASIO4ALL takes exclusive control of its output, so Windows can't play there. Solution: Windows plays to a different "bridge" output, and the plugin captures that output and mixes it into the DAW output.

### Architecture

```
Windows apps -> bridge output (e.g. Realtek) -> WASAPI loopback -> plugin -> DAW -> ASIO4ALL -> headphones/speakers
```

- **Capture thread**: opens `IAudioClient` with `AUDCLNT_STREAMFLAGS_LOOPBACK` on the chosen render endpoint, never touching the audio thread.
- **Lock-free SPSC ring buffer** (stereo float) between the capture thread and the audio thread.
- **Adaptive resampling** (Hermite interpolation): the read ratio is nudged so the ring fill stays at the configured buffer size, absorbing drift between Windows and DAW clocks (no clicks).
- **Dry path untouched**: the DAW signal passes through with zero added latency (reported latency 0); only the captured audio is buffered, then added to the input.
- Loopback produces no data while the endpoint is silent; the capture thread fills the gap with silence so timing stays continuous.

### Controls

Device dropdown (all active render endpoints), gain (-60..+12 dB), mute, buffer size (10-250 ms, default 50), level meter, and a status line that warns when the device is unavailable (it keeps retrying). The selected device is saved in the plugin state.

### Setup

1. In Windows Sound settings, set the **default output** to the bridge device (e.g. Realtek speakers/headphone jack).
2. In Reaper, use ASIO4ALL with the output you actually listen on (e.g. Logitech headset).
3. Insert DesktopAudioBridge on the guitar track (or master) and pick the bridge device in the dropdown.

Do not route the bridge device to your monitoring output through Windows ("Listen to this device"), or you'll hear it twice.

### Download

Prebuilt `.vst3` zips are on the [Releases page](https://github.com/lucastaonline/DesktopAudioBridge/releases) (no compiling needed). Maintainers: pushing a tag like `v0.1.0` builds and publishes a release automatically.

### Build

Requirements: Windows, Visual Studio 2022 (C++ workload), CMake >= 3.22, Git.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\DesktopAudioBridge_artefacts\Release\VST3\DesktopAudioBridge.vst3`. JUCE is downloaded automatically. Install by copying that folder to `C:\Program Files\Common Files\VST3\` (admin) and rescanning in Reaper (Preferences > Plug-ins > VST > Re-scan).

---

## Português

### Caso de uso

Você toca guitarra no Reaper com ASIO4ALL (latência mínima) e quer ouvir também o áudio do Windows (navegador, música, chamadas) no mesmo fone/caixa. O ASIO4ALL monopoliza a saída escolhida, então o Windows não consegue tocar nela. Solução: o Windows toca numa saída "ponte" diferente, e o plugin captura essa saída e soma na saída da DAW.

### Arquitetura

```
Windows -> saída ponte -> loopback WASAPI -> plugin -> DAW -> ASIO4ALL -> fone/caixa
```

- **Thread de captura**: abre `IAudioClient` com `AUDCLNT_STREAMFLAGS_LOOPBACK` na saída escolhida, sem bloquear a thread de áudio.
- **Buffer circular lock-free** (estéreo float) entre a thread de captura e a de áudio.
- **Resampling adaptativo** (interpolação Hermite): a razão de leitura é ajustada para manter a folga configurada, corrigindo o drift entre os clocks do Windows e da DAW (sem estalos).
- **Áudio da DAW intacto**: a guitarra passa com latência zero adicionada (latência reportada 0); só o áudio capturado tem buffer próprio e é somado à entrada.
- O loopback não gera dados quando a saída está muda; a thread de captura preenche com silêncio para manter o tempo contínuo.

### Controles

Dropdown de dispositivos (saídas ativas), ganho (-60..+12 dB), mute, tamanho do buffer (10-250 ms, padrão 50), medidor de nível e linha de status que avisa quando o dispositivo não está disponível (e continua tentando). O dispositivo escolhido é salvo no estado do plugin.

### Configuração

1. No Windows (Configurações de som), defina a **saída padrão** como o dispositivo ponte (ex.: caixa/saída P2 Realtek).
2. No Reaper, use ASIO4ALL com a saída em que você realmente ouve (ex.: fone Logitech).
3. Insira o DesktopAudioBridge na track da guitarra (ou no master) e escolha o dispositivo ponte no dropdown.

Não roteie a saída ponte para o monitor via Windows ("Ouvir este dispositivo"), ou você ouvirá em dobro.

### Download

Os `.zip` com o `.vst3` pronto ficam na [página de Releases](https://github.com/lucastaonline/DesktopAudioBridge/releases) (não precisa compilar). Para mantenedores: enviar uma tag como `v0.1.0` compila e publica a release automaticamente.

### Compilar

Requisitos: Windows, Visual Studio 2022 (workload C++), CMake >= 3.22, Git.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Saída: `build\DesktopAudioBridge_artefacts\Release\VST3\DesktopAudioBridge.vst3`. O JUCE é baixado automaticamente. Para instalar, copie essa pasta para `C:\Program Files\Common Files\VST3\` (admin) e faça re-scan no Reaper (Preferences > Plug-ins > VST > Re-scan).

## License / Licença

GPLv3. JUCE is used under its GPLv3 option.
