# KR-106 para Android (versão de toque)

App Android do Ultramaster KR-106 com interface de toque em paisagem, tape delay e
teclado de teste na tela. Feito para uso com controlador MIDI (USB ou Bluetooth).

## Gerar o APK (sem instalar nada no PC)

1. Faça um fork do repositório e suba estas alterações numa branch chamada `android`
   (ou rode o workflow manualmente em *Actions > Android APK (KR-106 touch) > Run workflow*).
2. Aguarde o job terminar (a primeira vez demora por causa do Projucer; depois usa cache).
3. Baixe o artefato `KR106-touch-apk` e extraia `KR106-touch.apk`.
4. No celular: permita "instalar apps desconhecidos" para o navegador/gerenciador de
   arquivos e abra o APK. Ou via PC: `adb install KR106-touch.apk`.

O APK é assinado com a chave de debug padrão, o que basta para instalar direto (sem Play Store).

## Usando

- **Controlador MIDI:** conecte pela porta USB-C (cabo/adaptador OTG). Controladores
  class-compliant aparecem sozinhos; o app procura novos dispositivos a cada 1,5 s.
  Bluetooth MIDI depende da permissão do Android e ainda não foi testado.
- **Keys:** botão vermelho na barra superior abre um teclado de 3 oitavas (multitoque,
  glissando, velocity pela altura do toque, Oct -/+ e bender). Fecha com Close ou arrastando
  o cabeçalho para baixo.
- **MIDI learn:** segure o dedo (~0,5 s) em qualquer controle e mexa no controle do teclado.
  Toque em qualquer outro controle para cancelar.
- **Entrada numérica:** toque duplo no fader/knob (comportamento original).
- **Engrenagem:** abre o menu original de ajustes (vozes, oversampling, sync etc.).
  No S10, comece com 6 vozes e VCF Oversample 2x e suba se a CPU permitir.

## Zoom e arrastar

Menu da engrenagem > 100% / 150% / 200% / 300%, ou com gestos: pinça com dois dedos em áreas
vazias (entre os painéis), toque duplo numa área vazia para alternar 100% <-> 200%.
Com zoom, arraste o dedo numa área sem controles para mover a visualização.

## Estados (Options)

Engrenagem > "Options: State, Audio, MIDI". Na seção State:

- **Save current state...** salva o estado completo (patch, delay, arpejador e ajustes globais,
  coisas que o preset não guarda) com o nome que você escolher.
- **Load a saved state...** lista os estados salvos (toque para carregar, Delete com confirmação).
- **Reset to default state** volta ao estado padrão; se não houver um, restaura os valores de fábrica.
- **Save as default state** grava o estado atual como padrão: ele é carregado **sempre que o app abre**.
- **Remove default state** apaga o padrão (o app volta a reabrir com a última sessão).

## Áudio / MIDI / Latência

O botão Voltar do Android (ou Esc no desktop) fecha a tela Options.

Na mesma tela Options: escolha o tamanho do buffer (latência), a taxa de
amostragem e o driver de áudio, ligue/desligue cada controlador MIDI e veja um monitor da
última mensagem MIDI recebida. As escolhas ficam salvas.

## Arpejador

Além do Mode (Up / Up-Down / Down) e do Range originais, o seletor **Pattern** oferece:
Classic (segue a chave Mode), Up/Down incl., Down/Up, As played, **Random S&H**, Random walk,
Converge, Diverge e Pedal. O seletor **S&H cycle** define como o Random S&H amostra: Free
(valor novo a cada passo) ou Loop 2-16 (os valores amostrados ficam num ciclo que repete) ou
Evolve 4/8/16 (ciclo que muda aos poucos).

## Tape delay

Knobs Time, Fdbk, Mix, Tone, Wow (profundidade do LFO que oscila o tempo de atraso, ou seja a
afinação dos ecos) e Speed (velocidade desse LFO), entre o chorus e o master. O delay é global: não faz
parte dos presets e não muda ao trocar de som. Mix em 0 deixa o som idêntico ao original.

## Build de teste no desktop (mesma interface)

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKR106_TOUCH=ON \
          -DKR106_FORMATS=Standalone -DKR106_COPY_AFTER_BUILD=OFF
    cmake --build build --target KR106_Standalone

## Arquivos

- `KR106Touch.jucer` – projeto Projucer (gerado por `python3 android/gen_jucer.py`)
- `android/Main.cpp` – app host (áudio via Oboe, MIDI, salvar/restaurar estado)
- `Source/Controls/KR106Touch*.h` – gestos de toque e teclado de teste
- `Source/DSP/KR106TapeDelay.h` – tape delay
- `.github/workflows/android-apk.yml` – build na nuvem

## Licença

GPL-3.0 (como o projeto original e o JUCE). Se distribuir o APK, publique o código-fonte
da sua versão.
