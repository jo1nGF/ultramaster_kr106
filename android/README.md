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

## Página Mod: LFOs, envelope e matriz

A terceira aba **Mod** tem:
- **LFO 1 e LFO 2** com os controles do XLFO: Wave (sine, triangle, square, saw up/down, smooth random,
  S&H), Retrig, Rate, Delay (fade-in), S/H (mistura com sample & hold), Smooth, Phase e Amount.
  O SYNC do desenho (sincronia com o tempo do DAW) ainda não está nesta versão.
- **Envelope** com os controles do XADSR: Attack, Decay, S-Start (nível de sustain), S-Time (tempo
  para o sustain cair a zero), Release, Amp e Velocity.
- **Matriz** de 12 linhas: Source (None, LFO 1, LFO 2, Envelope), Target (escolhe um grupo e depois o
  destino) e Amount, um slider horizontal: arraste para a esquerda ou direita (cerca de 300 px dão o curso todo, -100 % a +100 %; cada pixel é cerca de 0,7 %). Duplo toque volta a 0 %, e segurar o dedo faz MIDI learn. A profundidade final é o amount da linha vezes o Amount do LFO.

Destinos disponíveis (34, em grupos): VCF, HPF, VCA, DCO LFO/PWM/Sub/Noise, Tuning, LFO Rate,
envelope principal, Tape delay, Digital delay, Clouds e Arp/limiter. Modos (60/106), liga/desliga
e configurações globais não são modulados.

Os controles da página Mod são globais (não entram nos presets) e fazem parte do estado salvo.

## Páginas: Synth e FX

As abas **Synth | FX** na barra superior trocam de página. Synth: LFO, DCO, HPF, VCF, VCA,
envelope, chorus e arpejador. FX: tape delay, delay digital, limiter, gravador de WAV e Clouds.

## Arpejador: novidades

- **Swing:** atrasa os passos pares (50% a 75%). **Gate:** duração da nota (10% a 100%).
  **Ratchet:** 1 a 4 repetições por passo. **Chord:** toca todas as notas juntas a cada passo
  (o Range espalha o acorde pelas oitavas). Swing, gate e ratchet valem para o relógio interno
  (não para o transporte de um DAW).
- **Automação dos tipos (Auto pattern):** o padrão muda sozinho a cada N passos
  (*Change every*: 4 a 64). Modos: Cycle (próximo da lista), Random e Ping-pong (vai e volta).
  *Patterns used* escolhe quais padrões entram (toque para marcar/desmarcar). Com a automação
  ligada o botão Pattern mostra o padrão atual com um "> " na frente; ao desligar, volta ao que
  você escolheu.

## Clouds (granular)

Página FX. Grava a entrada num buffer de 2 s e toca grãos dele: Position (quão atrás), Size
(20 ms a 1,2 s), Pitch (-24 a +24 semitons), Density (1 a 80 grãos/s), Texture (janela e
aleatoriedade), Spread (panorama), Feedback, Reverb e Mix. **Freeze** para a gravação e faz os
grãos tocarem o buffer congelado. O reverb do Clouds é mais profundo que o da primeira versão: a cauda dura cerca de 3 s (antes 1,3 s), com mais difusão no início (menos ecos separados) e tom um pouco mais escuro. O visor mostra os grãos sobre os últimos 2 s. Mix em 0 deixa
o som idêntico.

Decay (0,5 s a 30 s) e Tone (escuro a brilhante) completam o reverb do Clouds. Com o Tone no
máximo a cauda fica um pouco mais brilhante (cerca de 1,3 dB mais energia acima de 2 kHz) e o
decay continua limitado a cerca de 8 s.

## Feedback de modulação

Knobs mostram o arco vermelho no valor que realmente está sendo tocado: se um LFO ou o envelope
modula o controle, o arco acompanha. Faders mostram a posição modulada da mesma forma.

Mode (seletor no Clouds) troca o firmware:
- **Granular:** o Clouds original, descrito acima.
- **Pitch shift:** grãos curtos e densos lendo o áudio recente; o Pitch transpõe (ex.: +12 st = uma oitava).
- **Looping delay:** linha de delay com feedback (Size = tempo, de 20 ms a cerca de 2 s); Freeze
  para a entrada e repete o conteúdo em loop.
- **Spectral:** STFT de 2048 pontos; Pitch desloca as bandas, Texture aleatoriza as fases,
  Feedback suaviza as magnitudes e Freeze mantém o espectro. Latência de cerca de 43 ms.

**Créditos e licença.** O modo **Looping delay** é uma adaptação do `looping_sample_player.h` do
firmware do Clouds (Copyright 2014 Emilie Gillet, licença MIT, github.com/pichenettes/eurorack); o
aviso de licença está no código. O modo **Pitch shift** é uma adaptação do WSOLA do firmware do Clouds
(`wsola_sample_player.h`, `correlator` e `window.h`, mesma licença MIT): transposição e time
stretch por busca de correlação. Pitch de -24 a +24 semitons; Position e Size controlam a janela. O modo **Spectral** é uma implementação própria (STFT) e não tem
o algoritmo do firmware original, que não está no repositório. Não usamos o nome da marca Mutable
Instruments no app.

## Limiter

Página FX. Limiter com look-ahead (1,5 ms) no fim da cadeia: Ceiling (-12 a 0 dB) e Release
(20 a 800 ms), medidor GR. Desligado, o som passa intacto.

## Gravador de WAV

Página FX: botão vermelho grava a saída final (depois do limiter) em WAV 24-bit estéreo. Depois:
**Share last** abre o menu de compartilhar do Android, **Save as...** salva onde você quiser.
Todas as gravações ficam na lista Options > WAV recordings (Share, Save as, Delete). A gravação
é fechada corretamente se o app for para segundo plano.

## Exportar e importar estados

Options > State: **Export all states (.zip)** compartilha um .zip com todos os estados (e o
estado padrão); **Export all > save as...** salva esse .zip onde você escolher; na lista de
estados, cada um tem **Share** e **Save as...**; **Import state / zip...** traz de volta arquivos
.kr106state ou .zip (vários de uma vez). Um estado inclui todos os parâmetros (incluindo os de
LFO, envelope, delays, Clouds, limiter e arpejador) e as notas travadas pelo Hold.

## Notas travadas (Hold) no estado

Os estados salvos (e o estado padrão) guardam também as notas que o Hold está segurando, com ou
sem o arpejador ligado. Ao carregar, elas voltam travadas e o arpejo continua; as notas que
estavam travadas antes de carregar são soltas. No teclado de teste, com Hold ligado, tocar de
novo numa nota que já está travada (acesa) a solta, como no teclado do PC.

## LFO: formas de onda

O seletor **Wave** (abaixo da chave Mode do LFO) oferece: Triangle (a original), Sine, Square,
Saw up, Saw down, **S&H random** (um valor aleatório novo a cada ciclo, mantido até o próximo) e
Smooth random (os mesmos valores sorteados, com transição suave). Vale para todos os destinos do
LFO (afinação, PWM, filtro) e nos dois modos, 60 e 106. A onda é parte do patch (presets).

## Envelope em loop

O botão **Loop** no cabeçalho do Envelope: com uma tecla pressionada, quando o decay chega ao
sustain o ataque recomeça, repetindo o ciclo A-D (o nível de S define o piso do ciclo; com S
alto o loop quase não aparece, e acima de ~95% fica desligado). Ao soltar a tecla vale o
Release normal. Também é parte do patch (presets) e aceita MIDI learn (toque longo).

## Delay digital

O tile de delay tem duas abas: **Tape delay** e **Digital delay**. As duas funcionam ao mesmo
tempo (digital depois do tape); a aba só escolhe qual você está editando. O digital é um eco
limpo e preciso: Time (5 ms a 1,5 s, sem mudar a afinação nem estalar ao mexer), Fdbk, Mix,
Tone (passa-baixa no feedback), Low cut (passa-alta no feedback) e Mode (toque para alternar):
Stereo, Ping-pong (os ecos alternam esquerda e direita) e Dotted (direita a 3/4 do tempo da
esquerda). O primeiro eco sai limpo e os seguintes ficam mais escuros e finos. Mix em 0 deixa
o som idêntico. Como o tape delay, não faz parte dos presets (entra nos estados salvos).

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
