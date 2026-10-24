# Lightshot HDR Fix

[English](../../README.md) · [Português (Brasil)](pt-BR.md) · [Español](es.md) · [Français](fr.md) · [Deutsch](de.md) · [简体中文](zh-CN.md) · [日本語](ja.md)

![Gardez le HDR activé. Capturez en SDR.](../assets/hero.svg)

**Gardez le HDR activé. Capturez en SDR.** Extension indépendante qui corrige les captures délavées ou surexposées de Lightshot grâce au FP16 et au tone mapping sur GPU. L’image SDR est transmise à l’éditeur habituel, sans bordure jaune Windows Graphics Capture.

[**Télécharger le programme Windows**](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases/latest/download/Lightshot-HDR-Fix-Setup.exe) · [Versions](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases) · [Validation](../VALIDATION.md)

## Installation

1. Installez d’abord le [Lightshot original compatible](https://app.prntscr.com/en/).
2. Téléchargez et lancez **Lightshot-HDR-Fix-Setup.exe**. Python et les droits administrateur ne sont pas nécessaires.
3. Le programme détecte une installation compatible ; sinon, sélectionnez le dossier de version. Si Lightshot est ouvert, il demande confirmation avant de le fermer.
4. Terminez puis ouvrez **Lightshot HDR Fix** depuis Démarrer. Le raccourci sur le Bureau et le lancement en fin d’installation sont facultatifs.

Le programme **0.3.0** copie Lightshot dans un dossier local de votre compte et ne modifie **que cette copie**. L’original reste intact ; le démarrage de Windows n’est pas modifié automatiquement. Pour revenir en arrière, fermez la copie et ouvrez l’original. Une seule instance à la fois : raccourcis, préférences et mutex peuvent être partagés.

> Le programme et l’extension ne sont pas signés. SmartScreen peut afficher un avertissement ; vérifiez que le téléchargement provient des Releases de ce dépôt.

## Compatibilité et limites

Windows 10/11 et carte graphique compatible DXGI 1.5. Lightshot x86 requis : dossier **5.5.0.7**, ressource de version de l’exécutable **5.5.0.4**, empreintes exactes. Les autres versions sont refusées. **Lightshot original non inclus.** Sortie SDR, HDR toujours activé.

Le plein écran exclusif, le contenu protégé, les écrans pivotés et le parcours complet édition/enregistrement/presse-papiers nécessitent davantage de tests. Le repli GDI peut rester surexposé en cas d’échec. Le curseur peut être doublé.

## Comparaison contrôlée

![Test contrôlé de plages de gris](../assets/gray-patches.svg)

Plages de gris de test, **pas une capture de jeu** : sortie originale **210 / 255 / 255 / 255**, corrigée **118 / 240 / 250 / 253**. La courbe change pour séparer les hautes lumières ; aucune équivalence colorimétrique universelle n’est promise.

Sur un seul PC RTX 5080 (3440×1440 HDR + 1920×1080 SDR) : captures complètes du moteur en **62–78 ms** à chaud, environ **312 ms** au premier appel. Ce n’est pas une garantie de latence totale. [Résultats et limites](../VALIDATION.md).

## ZIP manuel et compilation

Le ZIP est une option avancée nécessitant Python 3.10+. Les [commandes, empreintes et détails techniques sont en anglais](../../README.md#manual-zip-installation).

Projet indépendant, sans lien officiel avec Skillbrains. Il ne recompile pas le cœur fermé de Lightshot. Aucune licence open source générale n’a été choisie pour le nouveau code ; les composants tiers conservent leurs licences.
