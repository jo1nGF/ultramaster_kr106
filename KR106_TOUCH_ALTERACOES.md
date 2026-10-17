# KR-106 Touch — alterações desta versão

## Implementado no código-fonte

- **Duplo toque em knobs e faders contínuos:** restaura o valor padrão definido pelo parâmetro. Botões e parâmetros inteiros preservam sua semântica de toque.
- **MIDI Learn:** toque longo abre um menu com `Start MIDI Learn` e `Remove learned CC / use default`. Remover a atribuição elimina o override salvo e volta ao CC padrão do app, quando houver um definido.
- **Aviso MIDI:** mensagem de estado em uma faixa fixa no topo, separada dos valores numéricos dos controles.
- **Arpejador:** novos controles `Strum` (espalhamento temporal das notas do acorde) e `Prob` (probabilidade de disparo por passo).
- **Quantização de escala:** Off, Major, Minor, Harmonic Minor, Major Pentatonic, Minor Pentatonic, Blues, Dorian e Custom; seletor de tônica C–B e máscara editável de 12 semitons.
- **Automação do arpejador:** modo, intervalo de mudança e seleção de padrões continuam disponíveis em um menu compacto, para não sobrepor os knobs de Strum/Prob.
- **Modulação:** LFO 3 adicionado como fonte; matriz ampliada para 69 destinos, incluindo os oito controles de cada LFO, os sete controles do envelope de modulação e a taxa de portamento.
- **Compatibilidade de roteamentos:** os IDs antigos de parâmetros permanecem nas posições originais. As codificações antigas de fontes/destinos da matriz são preservadas; os novos destinos usam codificações adicionais com decodificação tolerante a arredondamento.

## Testes executados

Teste unitário leve da parte DSP, sem dependência do JUCE:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -ISource tests/feature_smoke.cpp -o /tmp/kr106_feature_smoke
/tmp/kr106_feature_smoke
```

Resultado neste ambiente:

```text
PASS: legacy routing compatibility, 69 modulation targets, scale quantize, probability and chord strum.
```

O teste cobre a preservação dos códigos antigos, o retorno dos 69 destinos, o intervalo de grupos, a quantização de notas, probabilidade zero e o espaçamento de notas do acorde.

## Limitação da compilação do APK

**Não foi gerado nem testado um APK instalável neste ambiente.** O ZIP de entrada não inclui `libs/JUCE`, `resources/` (imagens/fontes usados pelo projeto), nem `.gitmodules`, e `ANDROID_HOME` não está configurado. O CMake falha ao tentar executar `add_subdirectory(libs/JUCE)` porque essa pasta não existe. Portanto, este pacote é uma entrega das alterações de código sobre os arquivos fornecidos, não um APK pronto para instalação.

Para produzir o APK, aplique as alterações à cópia completa do projeto que contenha os recursos originais e o submódulo JUCE. O workflow Android existente em `.github/workflows/android-apk.yml` foi mantido; a compilação deve ser executada nesse ambiente completo com o SDK/NDK Android configurado.

## Correção adicional do erro de build (17/10/2026)

O build Android reportou identificadores `mParam` e `mHandleImg` não declarados em `Source/Controls/KR106Slider.h`. A causa era uma edição anterior que substituiu indevidamente parte da classe e removeu as declarações dos membros, assim como as classes derivadas `KR106HPFSlider`, `KR106ArpRateSlider` e `KR106LfoRateSlider`. O cabeçalho foi reconstruído a partir da versão-fonte recebida, preservando o reset por duplo toque para faders contínuos. O teste DSP `feature_smoke.cpp` foi recompilado e executado com sucesso após a correção. A compilação Android completa não foi executada neste ambiente porque o ZIP fornecido não inclui `libs/JUCE` nem os recursos Android completos.
