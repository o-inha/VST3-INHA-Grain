# INHA control interaction contract

The behaviour in the shipping plug-ins is the source material for this contract.
The contract exists so later plug-ins do not silently reinterpret familiar INHA controls.

## Continuous knobs and sliders

- Drag vertically. Up increases and down decreases.
- Hold **Shift** for fine adjustment. The normal full-range distance is 160 pixels;
  fine adjustment uses 600 pixels.
- Double-click restores the parameter's declared default unless a product explicitly
  documents a different action.
- Non-linear or “progressive” controls map the normalized gesture through a declared
  logarithmic or power curve. The curve changes value distribution, not drag direction.

## Range knobs

- With no range modifier, dragging selects and moves one handle. If both handles overlap,
  the initial vertical direction selects the upper or lower handle.
- Hold **Ctrl** to translate both handles by the same amount while preserving their width.
- Hold **Alt** to move both handles symmetrically around their current midpoint.
- Shift remains the fine-adjustment modifier in every mode. Therefore Ctrl+Shift performs
  a fine translation and Alt+Shift performs a fine symmetric resize.
- Ctrl wins if Ctrl and Alt are both held accidentally.
- Values remain ordered and clamped to the normalized range 0–1.

## Labels and values

- Value and label hit areas must be intentional rather than accidental extensions of the
  knob hit area.
- Product-specific shortcuts such as clicking a label to toggle zero are allowed, but must
  be declared beside the control and tested in that product. Core provides the normalized
  zero/remember/restore calculation; the product owns the hit area and chosen shortcut.
- A range reset updates both automatable parameters as one user gesture.

## Windows modifier mapping

- Shift: `VK_SHIFT`
- Ctrl: `VK_CONTROL`
- Alt: `VK_MENU`
