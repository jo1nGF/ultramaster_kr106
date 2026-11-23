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
  Uma barra no rodapé da tela (fora da área dos controles, então não cobre nenhum número) mostra
  o que fazer, e depois o CC atribuído. Ela tem sempre três botões: **Remove CC** (o controle
  deixa de responder a qualquer CC, até o de fábrica), **Default CC** (esquece o CC que você
  atribuiu e o CC de fábrica do controle volta a valer, se ele tem um) e **Cancel / OK**. Toque em
  outro controle também cancela. No desktop, durante o learn: Delete = remove o CC, Backspace =
  volta ao padrão.
- **Valor padrão:** toque duplo em qualquer fader ou knob volta ao valor padrão do parâmetro
  (inclusive o slider de Amount da matriz). No desktop, Ctrl/Cmd/Alt/Shift + duplo clique ainda
  abre a entrada numérica.
- **Engrenagem:** abre o menu original de ajustes (vozes, oversampling, sync etc.).
  No S10, comece com 6 vozes e VCF Oversample 2x e suba se a CPU permitir.

## Página Mod: LFOs, envelope e matriz

A terceira aba **Mod** tem:
- **LFO 1, LFO 2 e LFO 3** com os controles do XLFO: Wave (sine, triangle, square, saw up/down, smooth random,
  S&H), Retrig, Rate, Delay (fade-in), S/H (mistura com sample & hold), Smooth, Phase e Amount.
  O SYNC do desenho (sincronia com o tempo do DAW) ainda não está nesta versão.
- **Envelope** (página Mod) com os controles do XADSR em sete faders verticais: Att (Attack), Dec
  (Decay), Sus (S-Start, o nível de sustain), S-Tm (S-Time, tempo para o sustain cair a zero), Rel
  (Release), Amp e Vel (Velocity). Acima dos faders fica um **display** com a curva do envelope, desenhada a partir dos faders (inclusive com
  a modulação que a matriz está somando neles). O ponto vermelho mostra por onde o envelope real
  está passando enquanto uma nota toca.
- **Matriz** de 12 linhas: Source (None, LFO 1, LFO 2, LFO 3, Envelope), Target (escolhe um grupo e depois o
  destino) e Amount, um slider horizontal: arraste para a esquerda ou direita (cerca de 300 px dão o curso todo, -100 % a +100 %; cada pixel é cerca de 0,7 %). Duplo toque volta a 0 %, e segurar o dedo faz MIDI learn. A profundidade final é o amount da linha vezes o Amount do LFO.

Destinos disponíveis (67, em 11 grupos): VCF, HPF, VCA, DCO LFO/PWM/Sub/Noise, Tuning,
**Portamento**, LFO Rate, envelope principal, **todos os controles das três LFOs** (Rate, Amount,
Delay, S/H, Smooth, Phase), **todos os controles do envelope da página Mod** (Attack, Decay,
S-Start, S-Time, Release, Amp, Velocity), Tape delay, Digital delay, Clouds e Arp/limiter
(incluindo Strum, Probability e **Arp Scale / Arp Root**, que mudam a escala e a tônica do
quantizador, por exemplo uma LFO passeando pelas escalas). Uma LFO pode modular outra, ou a si mesma. Um controle da página
Mod modulado passa a valer a partir do bloco de áudio seguinte (alguns milissegundos), o que não
faz diferença audível. Modos (60/106), liga/desliga e configurações globais não são modulados.

Estados salvos antes da LFO 3 são convertidos automaticamente (as linhas da matriz mantêm
Source e Target).

Os controles da página Mod são globais (não entram nos presets) e fazem parte do estado salvo.

## Páginas: Synth e FX

As abas **Synth | FX** na barra superior trocam de página. Synth: LFO, DCO, HPF, VCF, VCA,
envelope, chorus e arpejador. FX: tape delay, delay digital, limiter, gravador de WAV e Clouds.

## Arpejador: novidades

- **Swing:** atrasa os passos pares (50% a 75%). **Gate:** duração da nota (10% a 100%).
  **Ratchet:** 1 a 4 repetições por passo. **Chord:** toca todas as notas juntas a cada passo
  (o Range espalha o acorde pelas oitavas). Swing, gate e ratchet valem para o relógio interno
  (não para o transporte de um DAW).
- **Strum** (knob, vale com Chord ligado): espalha as notas do acorde ao longo do passo, da mais
  grave à mais aguda. 0% = todas juntas; 100% = a última nota entra quase no fim do passo. Com
  ratchet, cada repetição é "strumada" de novo. O tempo das notas tem a resolução do bloco de
  áudio, então espaçamentos muito curtos (menos que alguns ms) saem juntos.
- **Prob** (knob): probabilidade de cada passo tocar (100% = sempre). Um passo que não toca vira
  pausa e o padrão segue andando, então o ritmo se mantém.
- **Scale** (botão ao lado de Prob): abre a folha do quantizador de escalas. As notas que você toca
  (teclado, MIDI, Keys) são puxadas para a nota mais próxima da escala (em empate, a de baixo), antes
  do arpejador, então o arpejo e os acordes ficam sempre na escala. Escolha a tônica (C a B) e
  uma entre 17 escalas (Major, Natural/Harmonic/Melodic minor, Dorian, Phrygian, Lydian, Mixolydian,
  Locrian, pentatônicas, Blues, Whole tone, Diminished, Phrygian dominant, Hungarian minor,
  Japanese) ou **Custom**. As 12 notas na parte de baixo mostram as notas permitidas; tocar numa
  delas edita a escala como Custom, partindo da escala escolhida. Custom usa notas absolutas (C a B),
  sem depender da tônica. **Off** não quantiza. A quantização acontece na **saída**, no momento em
  que a nota soa: notas seguradas com **Hold**, acordes (**Chord**) e passos do arpejo guardam a
  nota original e seguem a escala ao vivo. Se você trocar a escala ou a tônica (na mão ou por
  modulação) com notas seguradas, elas se movem para as notas da nova escala. O botão Scale mostra a
  escala em uso, inclusive quando ela está sendo modulada. Strum, Prob e Scale são globais (não entram nos
  presets) e podem ser modulados (Strum e Prob) ou ter MIDI learn.
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

## Aba Seq: sequenciador no estilo Varigate 8+

Uma quarta aba, **Seq**, com dois sequenciadores de passos que se alternam pelo seletor
**VARIGATE | METROPOLIX** no canto inferior esquerdo da aba. O seletor só escolhe qual dos dois você vê:
**os dois sequenciadores tocam ao mesmo tempo** (cada um tem o seu RUN, o seu tempo e o seu SYNC). A escolha é
salva com o estado. Esta seção descreve o Varigate; o Metropolix está
na seção seguinte.

O Varigate, no estilo do painel do Malekko Varigate 8+:

- **Sliders:** 16 sliders em duas colunas de 8 (passos 1 a 8 e 9 a 16). Cada slider tem uma alça preta
  com um **LED vermelho retangular grande**, e uma **tira branca** embaixo com a informação dele:
  "STEP 5  PROB 80%" nas páginas normais e, com **TRACK** ligado, a função que o slider controla
  (DIVIDE/MULTIPLY, SEQ LENGTH, SEQ MODE, KEY, ROOT, ROOT + 1 a ROOT + 11) com o valor atual. Na
  página GATE o LED mostra o passo ligado e pisca forte enquanto o passo toca; nas outras páginas o
  brilho acompanha o valor.
- **Botões:** redondos, acinzentados como no original, e **acendem** (branco azulado) quando ativos.
  Linhas: PROB RPT DLY / PW NOTE GLIDE / TRACK MUTE CLEAR / SONG TEMPO RUN-STOP / BANK SAVE RECALL, e na
  fileira dos jacks RND, FREEZE e END. Tocar de novo no botão de página aceso volta para a página GATE
  (há também um botãozinho GATE embaixo, à esquerda).
- **Painel da esquerda:** **CLK IN** (liga o SYNC com o host, com LED), **RESET**, **CLK OUT** (com LED
  que pisca a cada semicolcheia) e o **medidor de 12 LEDs** com as notas C a B. O medidor mostra o valor do
  último slider tocado (barra de LEDs) ou, na página NOTE e no KEY, a nota; depois mostra o passo que
  está tocando. Ao lado de cada LED há a tecla da escala (branca ou preta), acesa quando a nota
  faz parte da escala do padrão.
- **Fileira de baixo:** GATE 1 a 8, CV 1 e CV 2. O LED vermelho de cada um mostra o gate; o botão
  aceso é a trilha selecionada. Depois de **BANK**, **SAVE** ou **RECALL**, os mesmos botões escolhem o
  banco ou o preset (1 a 10): BANK + botão = banco; SAVE + botão grava o padrão atual nesse preset;
  RECALL + botão carrega.
- **Trilhas:** 8 de gate e 2 de CV (que tocam notas nas vozes do Juno). Cada trilha tem relógio
  próprio (÷8 a ×8), tamanho de 1 a 16, direção (Forward, Reverse, Pendulum, Random) e mute.
- **Páginas por passo:** **GATE** (toque ou arraste para pintar vários passos), **PROB**, **RPT**
  (x1 a x8), **DLY**, **PW**, **NOTE** (já na escala) e **GLIDE** (abre o Porta enquanto aquela nota
  toca; o modo Porta precisa estar ligado).
- **CV ligada ou independente:** o botão pequeno **LINK** (com uma CV selecionada) faz a CV seguir um
  dos 8 gates ou rodar sozinha com relógio próprio.
- **Outros:** **MUTE** (com ele aceso os botões de trilha silenciam a trilha), **CLEAR** (armado,
  o próximo botão de trilha apaga a trilha), **RND** (sorteia a página atual; em TRACK sorteia tudo),
  **RND ALL**, **ROT <** e **ROT >** (giram os passos), **FREEZE** (as probabilidades e a direção
  Random passam a se repetir iguais a cada ciclo, e RND fica desativado), **TEMPO** (arraste o
  knob; com SYNC ligado segue o BPM do host), **RUN/STOP**, **END** (LED que pisca quando a trilha
  mais longa termina um ciclo).
- **SONG** toca em sequência a lista de presets do banco (o próximo entra quando a trilha de ciclo mais
  longo termina). **S.PRG** + botões de trilha (1 a 10) monta a lista, e **CLEAR** com S.PRG ligado
  esvazia.
- **Modulação:** os 8 gates viram as fontes **Gate 1 a Gate 8** da matriz.
- Tudo é salvo no estado do plugin (padrão atual, 100 presets, songs, tempo e SYNC).

- **Botões pequenos** (canto inferior esquerdo): **LINK** (liga a CV selecionada a um gate), **ROT<** e
  **ROT>**, **R.ALL** (sorteia tudo da trilha), **S.PRG** (monta a lista do SONG), **ALL**, **REC**, **TAP** e
  **G+D**.
- **ALL:** o próximo movimento de slider vale para os 16 passos da trilha (dura um movimento, como segurar o
  botão BANK/ALL do original).
- **Gravação ao vivo (REC + TAP):** com REC aceso e o sequenciador rodando, as teclas que você toca (teclado do
  plugin, MIDI) ou o botão **TAP** gravam o gate no passo que está tocando (ou no seguinte, se o passo já passou
  da metade). Numa trilha de gate, as CVs ligadas a ela recebem a nota tocada; numa trilha de CV, a nota é
  gravada e o gate da trilha ligada é aceso. Selecione a trilha que quer gravar na fileira de baixo.
- **G+D (Gate+Delay):** com ele aceso, um passo que tem delay toca também um pulso curto na batida, antes do gate
  atrasado (o gate atrasado mantém a largura normal).
- **Escala por banco:** em TRACK, KEY e ROOT + n mudam a escala do **banco** atual: todos os presets do banco
  tocam nessa escala (ao chamar um preset ou trocar de banco, a escala do banco é aplicada).
- A lista de fontes da matriz tem agora **Out A** e **Out B** (do Metropolix); no Varigate elas ficam em zero.


## Aba Seq, modo Metropolix

O seletor **METROPOLIX** troca a aba para um sequenciador no estilo do Intellijel Metropolix (manual v1.6),
com o painel prateado do original.

- **Os 8 stages:** à direita, 8 sliders de **PITCH** (alça preta com LED retangular vermelho; o stage que toca
  acende), 8 de **PULSE COUNT** (1 a 8 pulsos de clock por stage) e 8 de **GATE TYPE** (HOLD, MULTI, SINGLE,
  REST, de cima para baixo). Esses três valem para as duas trilhas.
- **2 trilhas de nota (TRK 1 e TRK 2):** tocam nas vozes do Juno, cada uma do seu jeito: **ORDER** (as 20 ordens
  do manual, de Linear a Random, direta ou reversa; toque no encoder para inverter), **LEN** (stages, offset e
  pulsos, "Auto" ou até 64), **DIV** (divisor de clock e divisor de pulsos), **SWING** (50 a 78%), **SLIDE**
  (quantidade e tipo Analog, Tempo ou Acid), **GATE** (comprimento até Hold, escala e "stretching").
- **EDIT:** toque em EDIT, depois num botão de função da fileira de baixo (**SLIDE, SKIP, PITCH, GATE, RATCH,
  PROB, ACCUM, CV**). Em seguida os 8 botões escolhem o stage, e o encoder edita o valor dele: nota que
  substitui o slider, gate (--, rest ou 1 a 100%), ratchets (x1 a x8), probabilidade (toque no encoder: por stage
  ou por pulso), acumulador (de -7 a +7 graus da escala; toque: gatilho por stage, pulso ou ratchet) e a lane de
  CV (valor; toque: degrau, rampa por stage ou rampa por pulso). SLIDE e SKIP ligam e desligam direto.
- **SETUP (menu da trilha):** transposição, oitavas e direção dos sliders, pulse count e gate type forçados,
  gate stretching, modo de ratchet, tipo de slide, acumulador (modo stage/track, wrap/pendulum, uni/bipolar,
  limites) e mute. Toque no encoder para escolher entre rolar a lista e editar o item.
- **BPM** (toque no encoder alterna interno/host), **SCALE** (as 17 escalas e Custom do plugin, mais a tônica),
  **RUN**, **RESET**, **EXIT**, **TRK 1 / TRK 2**. **CLK** (jack) liga o sync com o host.
- **ALT + LOAD ou SAVE:** o encoder escolhe o banco (1 a 8) e um botão de stage escolhe o preset: 64 presets.
  **ALT + LIM** abre os limites do acumulador.
- **LOOPY** (botão do infinito): com ele aceso, segurar um botão de stage faz a trilha repetir aquele stage
  (segure dois para um trecho); ao soltar, a trilha volta ao ponto onde a sequência estaria. Sem LOOPY nem EDIT,
  um botão de stage toca aquele stage (stage player).
- **MOD:** os sliders passam a editar as **8 lanes MOD** (a lane se escolhe com o encoder): valor, rampa e gate.
  ORDER, LEN e DIV editam a lane; um toque no encoder na tela inicial troca o tipo (bipolar, unipolar, gate).
- **Modulação:** as 8 lanes MOD saem como as fontes **Lane 1 a Lane 8** na matriz, e as lanes de CV das trilhas como
  **Trk1 CV a Trk4 CV**. O slide do stage que toca abre o Porta (o modo Porta precisa estar ligado).
- Tudo é salvo no estado do plugin (padrão atual, 64 presets, tempo e SYNC).

- **CTRL 1 e CTRL 2:** o botão preto ao lado de cada knob abre as configurações (parâmetro, trilhas TRK 1, TRK 2
  ou 1+2). O knob define o valor direto (nos destinos bipolares, 12 horas é o valor programado). Destinos: BPM,
  Clock Div, Direction, Gate Length, Gate Scale, Octave, Pitch Pre, Pitch Post, Play Order, Probability, Pulse
  Count, Pulse Div, Pulses Len, Ratchet, Root, Slide Amount, Slider Range, Stages Len, Stage Offset, Swing, Mute,
  Reset, Skip Invert, Slide, Accumulate e Accum Reset (os marcados "gate" disparam acima de 30%).
- **AUX X, Y, Z:** toque no jack para escolher a fonte (LFO 1, LFO 2, LFO 3 ou o envelope do plugin), o parâmetro
  e as trilhas. O knob ao lado é o atenuador/inversor (12 horas = zero). Eles somam ao valor do parâmetro.
- **Lanes MOD:** ficam como fontes na matriz (Lane 1 a Lane 8) e também podem controlar os mesmos parâmetros de
  cima: no modo MOD, toque em MOD de novo (ou EDIT + botão de stage) para abrir a tela de atribuição da lane.
- **OUT A e OUT B:** toque nos jacks A e B (ou ALT + A, ALT + B) para escolher o que cada saída dá: Trk 1 ou 2
  Pitch, Trk 1 ou 2 Gate, Trk 1 ou 2 CV, ou uma das lanes MOD. Elas viram as fontes **Out A** e **Out B** na matriz.
- **ALT + TYPE:** o tipo da lane de CV da trilha: valor ou gate toggle (o EDIT CV usa então o gate do stage).
- **LOOPY:** a tela do LOOPY define quantos pulsos dura o loop (Auto ou até 64) e a quais trilhas ele vale.

Limitações: não há saídas físicas, então CLK (jack) só liga o sync, o LED CLK OUT é do Varigate, e as funções
ALT + CLK e ALT + MX (mixer) são apenas informativas; velocidade não é um destino; o stage player e o loopy
não gravam. Use a matriz de modulação para levar qualquer fonte ao resto do sintetizador.


## Novidades (versão 11)

- **Gravador em segundo plano:** o take continua gravando quando o app é minimizado; ele só é fechado quando você
  para ou fecha o app. O botão no tile do gravador (FX) escolhe entre **WAV 24-bit** e **OGG 128 kbps** (o arquivo
  mais leve, cerca de 10 vezes menor). MP3 não foi possível: o JUCE não traz um codificador MP3 e o Android não tem
  um; o Ogg Vorbis a 128 kbps toca em qualquer celular e tem a mesma qualidade. A lista de gravações mostra os dois
  formatos. (Para a gravação não ser interrompida pelo Android por muito tempo em segundo plano, o app precisa de
  um serviço em primeiro plano, que é uma opção do Projucer.)
- **MIDI learn por toque:** o botão **CC** (barra de cima, abaixo do Keys) liga o modo de mapear: os controles
  aparecem contornados em verde e **um toque** num controle já o seleciona; depois mova o controle do teclado. Uma
  faixa no rodapé diz o que fazer (e tem o botão Exit). Quando o CC é aprendido, o modo **desliga sozinho**, e os toques
  voltam a ajustar os parâmetros. As abas continuam funcionando durante o modo. **O segurar o dedo num controle não
  inicia mais o MIDI learn** (ele atrapalhava os ajustes).
- **Teclado:** o botão **Hold** está no cabeçalho do teclado. O teclado agora é uma gaveta **arrastável e
  "docável"**: arraste o cabeçalho (a barrinha no topo) para cima e ele sai do dock e passa a flutuar; arraste para
  cima e para baixo para escolher a altura; solte perto da base e ele volta para o dock; puxe bem para baixo para
  fechar. O botão **Dock** também encaixa e solta. O app lembra onde você deixou. Deslizar **de baixo para cima**
  na borda inferior da tela (numa área vazia) abre o teclado.
- **Zoom no Seq:** pinça e arraste nas partes vazias dos painéis dos sequenciadores (sliders e botões continuam
  com seus toques); toque duplo numa área vazia alterna o zoom.
- **Metropolix:** os faders agora **deslizam**: arraste o dedo para o lado para pintar os faders vizinhos da mesma
  fileira. Há **4 trilhas** (TRK 1 a TRK 4; a 3 e a 4 começam mudas), e as atribuições escolhem uma ou várias
  trilhas (TRK 1, TRK 2, TRK 3, TRK 4, 1+2, 3+4 ou todas). OUT A e OUT B também podem dar as trilhas 3 e 4.
  Estados salvos com duas trilhas carregam normalmente.
- **Os dois sequenciadores juntos:** fontes novas na matriz: **Gate 1-8** (Varigate), **Lane 1-8**, **Trk1 CV a
  Trk4 CV**, **Out A** e **Out B** (Metropolix). A lista de fontes passa a ter duas colunas.
- **Pitch Bend modulável:** novo destino **Pitch Bend** (grupo Performance) na matriz.
- **Strum e Ratchet sem o arp:** em Configurações, **Strum/Ratchet w/o Arp**. Ligado, as notas MIDI que chegam
  juntas (um acorde, dentro de 45 ms) são reconhecidas e tocadas uma depois da outra, com o atraso do parâmetro
  **Strum** (0 a 80 ms por nota). O **Ratchet** repete cada nota 2 a 4 vezes dentro de um passo do arp (a velocidade do
  arp define o tempo). Se você solta uma tecla antes da vez dela, ela não toca.
- **Ruído do chorus:** em Configurações, **Chorus Noise** desliga o ruído do chorus do Juno (chiado, vazamento,
  cliques e ripple).
- **FX 2:** toque no tab **FX** de novo para abrir a segunda página de efeitos (o tab passa a dizer "FX 2"):
  **Chorus** limpo estéreo (Rate, Depth, Mix), **Phaser** de 6 estágios (Rate, Depth, Fdbk, Mix), **Spring Reverb**
  (Decay, Tone, Drive, Mix) e **Compressor** (Thresh, Ratio, Attack, Release, Makeup, com medidor de redução), que
  fica antes do limiter. Cada um tem seu botão On e passa o som intacto quando desligado.
- **Menus de estados e gravações:** a tela de Estados agora tem colunas com títulos (Estado atual, Estado inicial,
  Backup, Gravações), e as listas de estados e de gravações são tabelas: o nome à esquerda e as opções (Share, Save
  as, Delete) alinhadas à direita; Importar e Voltar ficam no topo, à direita.

Testes: `tests/run.sh` compila e roda, com detectores de erros de memória, os motores dos sequenciadores, o DSP
(strum/ratchet sem o arp, quantizador com Hold, FX 2), os quatro efeitos e os painéis dos sequenciadores (com um
JUCE de mentira que só serve para exercitá-los).

## Novidades (versão 12)

- A tela do MIDI learn por toque ficou limpa: os contornos não se sobrepõem mais e o texto saiu de cima dos
  títulos (a mensagem agora aparece na faixa do rodapé).
- No Metropolix, o título "PULSE COUNT" não cobre mais os números dos stages, e as três faixas de sliders ganharam
  mais espaço.
- O teclado é arrastável e tem dock (veja "Teclado" acima).
- As páginas FX 2 (toque de novo no tab FX) e as trilhas 3 e 4 do Metropolix estão desde a versão 11. Se elas não
  aparecem, o app instalado ainda é a versão 10.
