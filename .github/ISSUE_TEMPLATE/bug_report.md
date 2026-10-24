---
name: Bug report
about: Report a problem with the HL-2270DW driver
title: "[BUG] "
labels: ''
assignees: ''

---

<!--
This driver supports only the Brother HL-2270DW. For other Brother
printers, see brlaser: https://github.com/Owl-Maintain/brlaser

Driver not showing up when you add the printer? In the Add Printer window,
click the printer in the list first (the "Use" menu is dimmed until you
do), then open the "Use" menu and choose "Brother HL-2270DW series,
hl2270dw-macos". macOS often picks Brother's driver instead. See
https://github.com/jmarler/hl2270dw-macos#installing
-->

**Describe the bug**
What happened, and what you expected to happen.

**Steps to reproduce**
1.
2.
3.

**Screenshots**
If applicable, add screenshots to help explain the problem.

**System**
- macOS version: <!-- Apple menu > About This Mac -->
- Mac: <!-- for example, MacBook Air (M1) -->
- Connection: <!-- network or USB -->
- Driver version: <!-- Printers & Scanners > click the printer > Driver version -->

**Terminal output**
Please run these commands in Terminal and paste the output:

```sh
pkgutil --pkg-info io.github.jmarler.hl2270dw-macos
lpinfo -m | grep -i 2270
grep -H '^\*NickName' /etc/cups/ppd/*.ppd
```

````
PASTE OUTPUT HERE
````

**Additional context**
Anything else that might help.
