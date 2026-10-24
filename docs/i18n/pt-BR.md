# Lightshot HDR Fix

[English](../../README.md) · [Português (Brasil)](pt-BR.md) · [Español](es.md) · [Français](fr.md) · [Deutsch](de.md) · [简体中文](zh-CN.md) · [日本語](ja.md)

![Mantenha o HDR ligado. Capture em SDR.](../assets/hero.svg)

**Mantenha o HDR ligado. Capture em SDR.** Complemento independente que corrige capturas lavadas ou superexpostas do Lightshot usando FP16 e mapeamento de tons na GPU. A imagem SDR vai para o editor conhecido, sem a borda amarela do Windows Graphics Capture.

[**Baixar instalador para Windows**](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases/latest/download/Lightshot-HDR-Fix-Setup.exe) · [Todas as versões](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases) · [Validação](../VALIDATION.md)

## Instalação em poucos cliques

1. Instale primeiro o [Lightshot original compatível](https://app.prntscr.com/en/).
2. Baixe e execute **Lightshot-HDR-Fix-Setup.exe**. Não precisa de Python nem de administrador.
3. O instalador detecta a instalação compatível. Se não encontrar, selecione a pasta da versão. Se o Lightshot estiver aberto, ele pede confirmação antes de fechá-lo.
4. Conclua e abra **Lightshot HDR Fix** pelo menu Iniciar. Atalho na área de trabalho e abertura ao concluir são opcionais.

O instalador **0.3.0** copia o Lightshot para uma pasta local do seu usuário e modifica **somente a cópia**. O original permanece intacto; a inicialização do Windows não muda automaticamente. Para voltar, feche a cópia e abra o original. Use uma instância por vez: atalhos, preferências e mutex podem ser compartilhados.

> O instalador e o complemento não são assinados. O SmartScreen pode exibir um aviso; confira se o download veio dos Releases deste repositório antes de executar.

## Compatibilidade e limites

Windows 10/11 e GPU com DXGI 1.5. Exige Lightshot x86 na pasta **5.5.0.7**, recurso de versão do executável **5.5.0.4** e hashes exatos. Outras versões são recusadas. **O Lightshot original não é distribuído.** A saída é SDR, com HDR ainda ligado.

Tela cheia exclusiva, conteúdo protegido, telas giradas e o fluxo completo de edição/salvamento/área de transferência precisam de mais testes. Em falhas, o fallback GDI pode continuar superexposto. Cursores podem aparecer duplicados.

## Comparação controlada

![Teste controlado de patches de cinza](../assets/gray-patches.svg)

Patches de cinza de teste, **não uma captura de jogo**: saída original **210 / 255 / 255 / 255**, corrigida **118 / 240 / 250 / 253**. A curva de tons muda para preservar a separação dos realces; não representa equivalência universal de cores.

Em um único computador com RTX 5080 (3440×1440 HDR + 1920×1080 SDR), capturas completas do núcleo levaram **62–78 ms** após aquecimento e cerca de **312 ms** na primeira chamada. Não é uma garantia de latência total. [Evidências e limitações](../VALIDATION.md).

## ZIP manual e compilação

O ZIP é uma alternativa avançada que exige Python 3.10+. Consulte os [comandos, hashes e detalhes técnicos em inglês](../../README.md#manual-zip-installation).

Projeto independente, sem vínculo oficial com a Skillbrains. Não é uma recompilação do núcleo fechado do Lightshot. Não foi escolhida uma licença geral de código aberto para o código novo; componentes de terceiros mantêm suas licenças.
