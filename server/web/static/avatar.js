/* XenithOS avatars for the web. A line-for-line port of shared/avatar.c:
 * keep the two in step so an avatar looks the same on a console and here.
 * Codes: "2" + 13 base-36 digits (current), or "1" + 9 digits (the first format). */
"use strict";

const XAvatar = (() => {
  const COUNTS = [10, 14, 13, 8, 10, 8, 12, 11, 12, 6, 5, 5, 3];
  const V1_COUNTS = [8, 8, 8, 5, 5, 4, 10, 6, 8];
  const DEFAULTS = [2, 0, 2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0];
  const SKIN = [0xF7D7C4, 0xEEC1A0, 0xE0A47E, 0xC68863, 0xA86B47, 0x8A5235, 0x6A3C25, 0x4A2A1A, 0xC9A27A, 0xF2C4B4];
  const HAIR = [0x1C1A1A, 0x3A2618, 0x6B4429, 0xA8743F, 0xDDB66B, 0xB4502A, 0xA9ADB3, 0x3D7BFF,
                0xE87BB0, 0x7B4FD0, 0x3FA35B, 0xEDEDED, 0xC0392B];
  const TOP = [0x2F6BFF, 0xE04A3F, 0x2EA866, 0xF0B429, 0xEDEFF5, 0x24272F, 0x7B8494, 0xF07A2E,
               0x7A55D6, 0x1FA6A0, 0xE86AA6, 0x22346B];
  const BG = [0x1B3A6B, 0x2F6BFF, 0x157A73, 0x2E7D4F, 0xC2672A, 0xA23B3B, 0x5B4A9E, 0x3C4352,
              0xB04A7A, 0xB8862B, 0x2E9C86, 0x24262C];
  const EYE = [0x2A2224, 0x6B3E1E, 0x2F6FD0, 0x2E8B57, 0x8E7240, 0x6E7B8B];
  const INK = 0x2A2224, MOUTH = 0x6B2A2A, GOLD = 0xE8B83A;
  const PI = Math.PI;

  function parse(code) {
    let parts = 0;
    if (typeof code === "string" && code.length === 10 && code[0] === "1") parts = 9;
    if (typeof code === "string" && code.length === 14 && code[0] === "2") parts = 13;
    return COUNTS.map((n, i) => {
      const d = i < parts ? parseInt(code[i + 1], 36) : NaN;
      return Number.isInteger(d) && d >= 0 && d < n ? d : DEFAULTS[i];
    });
  }

  function valid(code) {
    if (typeof code !== "string") return false;
    const counts = code.length === 10 && code[0] === "1" ? V1_COUNTS
      : code.length === 14 && code[0] === "2" ? COUNTS : null;
    if (!counts) return false;
    return counts.every((n, i) => { const d = parseInt(code[i + 1], 36); return d >= 0 && d < n; });
  }

  /* gfx_mix / gfx_shade, integer maths as in C */
  function mix(a, b, t) {
    t = Math.max(0, Math.min(256, t));
    const ch = (s) => {
      const x = (a >> s) & 255, y = (b >> s) & 255;
      return (x + (((y - x) * t) >> 8)) & 255;
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
  }
  const shade = (c, pct) => pct >= 0 ? mix(c, 0xFFFFFF, (pct * 256 / 100) | 0) : mix(c, 0, ((-pct) * 256 / 100) | 0);
  const css = (c) => "#" + c.toString(16).padStart(6, "0");

  function draw(ctx, code, x, y, size) {
    const v = parse(code);
    const X = (f) => x + f * size, Y = (f) => y + f * size, L = (f) => f * size;
    const fill = (c) => { ctx.fillStyle = css(c); ctx.fill(); };
    const rr = (fx, fy, fw, fh, r, c) => { ctx.beginPath(); ctx.roundRect(X(fx), Y(fy), L(fw), L(fh), L(Math.min(r, fw / 2, fh / 2))); fill(c); };
    const ell = (cx, cy, rx, ry, c) => { ctx.beginPath(); ctx.ellipse(X(cx), Y(cy), L(rx), L(ry), 0, 0, 2 * PI); fill(c); };
    const ellA = (cx, cy, rx, ry, c, a) => { ctx.save(); ctx.globalAlpha = a / 255; ell(cx, cy, rx, ry, c); ctx.restore(); };
    const circ = (cx, cy, r, c) => ell(cx, cy, r, r, c);
    const stroke = (t, c) => { ctx.lineWidth = L(t); ctx.lineCap = "round"; ctx.strokeStyle = css(c); ctx.stroke(); };
    const ln = (x0, y0, x1, y1, t, c) => { ctx.beginPath(); ctx.moveTo(X(x0), Y(y0)); ctx.lineTo(X(x1), Y(y1)); stroke(t, c); };
    const arc = (cx, cy, r, t, a0, a1, c) => { ctx.beginPath(); ctx.arc(X(cx), Y(cy), L(r), a0, a1); stroke(t, c); };
    const ring = (cx, cy, r, t, c) => { ctx.beginPath(); ctx.arc(X(cx), Y(cy), L(r), 0, 2 * PI); stroke(t, c); };
    const tri = (a, b, c2, d, e, f, c) => { ctx.beginPath(); ctx.moveTo(X(a), Y(b)); ctx.lineTo(X(c2), Y(d)); ctx.lineTo(X(e), Y(f)); ctx.closePath(); fill(c); };
    const clipped = (fy, above, fn) => {
      ctx.save(); ctx.beginPath();
      if (above) ctx.rect(X(0), Y(0), L(1), L(fy)); else ctx.rect(X(0), Y(fy), L(1), L(1 - fy));
      ctx.clip(); fn(); ctx.restore();
    };
    const hairCap = (cut, c) => clipped(cut, true, () => ell(0.5, 0.355, 0.205, 0.145, c));

    const skin = SKIN[v[0]], hair = HAIR[v[2]], top = TOP[v[6]];
    const skinDark = shade(skin, -14), topDark = shade(top, -22);
    const eye = EYE[v[9]], dot = v[9] ? shade(eye, -45) : INK;
    const style = v[1], acc = v[7], hat = acc === 4 || acc === 5;

    ctx.save();
    ctx.beginPath(); ctx.roundRect(x, y, size, size, L(0.22)); ctx.clip();
    rr(0, 0, 1, 1, 0.22, BG[v[8]]);

    // Behind the head
    if (style === 2) rr(0.26, 0.26, 0.48, 0.54, 0.18, hair);
    if (style === 11) rr(0.27, 0.27, 0.46, 0.40, 0.16, hair);
    if (style === 8 && !hat) { circ(0.5, 0.35, 0.255, hair); circ(0.29, 0.45, 0.09, hair); circ(0.71, 0.45, 0.09, hair); }
    if (style === 10) { ell(0.71, 0.52, 0.06, 0.16, hair); circ(0.675, 0.37, 0.024, shade(hair, -35)); }
    if (style === 13) {
      circ(0.255, 0.50, 0.075, hair); circ(0.745, 0.50, 0.075, hair);
      circ(0.30, 0.45, 0.02, top); circ(0.70, 0.45, 0.02, top);
    }
    if (v[5] === 1) rr(0.30, 0.66, 0.40, 0.16, 0.08, topDark);

    // Body
    rr(0.20, 0.74, 0.60, 0.40, 0.16, v[5] === 7 ? skin : top);
    rr(0.44, 0.58, 0.12, 0.20, 0.04, skinDark);
    switch (v[5]) {
      case 1:
        ln(0.46, 0.80, 0.45, 0.92, 0.012, 0xE8ECF4);
        ln(0.54, 0.80, 0.55, 0.92, 0.012, 0xE8ECF4); break;
      case 2:
        tri(0.43, 0.74, 0.50, 0.81, 0.39, 0.83, 0xF4F6FA);
        tri(0.57, 0.74, 0.50, 0.81, 0.61, 0.83, 0xF4F6FA); break;
      case 3:
        rr(0.44, 0.76, 0.12, 0.30, 0.0, 0xE8ECF4);
        ln(0.44, 0.76, 0.40, 0.95, 0.02, topDark);
        ln(0.56, 0.76, 0.60, 0.95, 0.02, topDark); break;
      case 4:
        rr(0.40, 0.735, 0.20, 0.045, 0.022, topDark);
        rr(0.20, 0.88, 0.60, 0.035, 0.0, shade(top, 22)); break;
      case 5:
        rr(0.20, 0.83, 0.60, 0.03, 0.0, shade(top, 45));
        rr(0.20, 0.91, 0.60, 0.03, 0.0, shade(top, 45)); break;
      case 6:
        tri(0.445, 0.74, 0.555, 0.74, 0.50, 0.80, skinDark);
        rr(0.215, 0.80, 0.035, 0.30, 0.0, 0xF4F6FA);
        rr(0.75, 0.80, 0.035, 0.30, 0.0, 0xF4F6FA); break;
      case 7:
        rr(0.315, 0.76, 0.37, 0.40, 0.09, top);
        rr(0.33, 0.74, 0.04, 0.06, 0.02, top);
        rr(0.63, 0.74, 0.04, 0.06, 0.02, top); break;
    }

    // Head
    circ(0.305, 0.47, 0.045, skin);
    circ(0.695, 0.47, 0.045, skin);
    ell(0.5, 0.45, 0.195, 0.225, skin);

    // Cheeks
    if (v[12] === 1) {
      ellA(0.385, 0.535, 0.036, 0.021, 0xFF7A8A, 95); ellA(0.615, 0.535, 0.036, 0.021, 0xFF7A8A, 95);
    } else if (v[12] === 2) {
      const fc = shade(skin, -32);
      circ(0.375, 0.515, 0.0065, fc); circ(0.395, 0.53, 0.0065, fc); circ(0.37, 0.54, 0.0065, fc);
      circ(0.625, 0.515, 0.0065, fc); circ(0.605, 0.53, 0.0065, fc); circ(0.63, 0.54, 0.0065, fc);
    }

    // Hair in front
    if (!hat) {
      switch (style) {
        case 0:
          hairCap(0.385, hair);
          rr(0.305, 0.36, 0.04, 0.10, 0.02, hair); rr(0.655, 0.36, 0.04, 0.10, 0.02, hair); break;
        case 1: hairCap(0.33, mix(hair, skin, 90)); break;
        case 2:
          hairCap(0.37, hair);
          rr(0.29, 0.33, 0.07, 0.38, 0.03, hair); rr(0.64, 0.33, 0.07, 0.38, 0.03, hair); break;
        case 3:
          hairCap(0.36, hair);
          for (let i = 0; i < 8; i++) {
            const a = PI * (200 + i * 20) / 180;
            circ(0.5 + 0.19 * Math.cos(a), 0.40 + 0.17 * Math.sin(a), 0.075, hair);
          }
          break;
        case 4:
          hairCap(0.36, hair);
          for (let i = 0; i < 5; i++) {
            const bx = 0.33 + i * 0.085;
            tri(bx, 0.29, bx + 0.085, 0.29, bx + 0.03, 0.15 + (i % 2) * 0.03, hair);
          }
          break;
        case 5: circ(0.5, 0.19, 0.075, hair); hairCap(0.38, hair); break;
        case 6: hairCap(0.36, hair); ln(0.33, 0.33, 0.60, 0.25, 0.09, hair); break;
        case 8: hairCap(0.35, hair); break;
        case 9: hairCap(0.33, mix(hair, skin, 110)); rr(0.455, 0.12, 0.09, 0.23, 0.045, hair); break;
        case 10: hairCap(0.375, hair); break;
        case 11:
          hairCap(0.37, hair);
          rr(0.285, 0.33, 0.065, 0.30, 0.03, hair); rr(0.65, 0.33, 0.065, 0.30, 0.03, hair); break;
        case 12:
          hairCap(0.37, hair);
          tri(0.30, 0.37, 0.56, 0.25, 0.33, 0.44, hair);
          ln(0.56, 0.24, 0.585, 0.33, 0.012, shade(hair, -30)); break;
        case 13: hairCap(0.37, hair); break;
      }
    } else if (style === 2 || style === 11) {
      const h = style === 2 ? 0.38 : 0.30;
      rr(0.29, 0.33, 0.07, h, 0.03, hair); rr(0.64, 0.33, 0.07, h, 0.03, hair);
    }

    // Eyebrows
    const brow = style === 7 ? shade(skin, -40) : hair;
    switch (v[10]) {
      case 1: ln(0.41, 0.405, 0.46, 0.40, 0.026, brow); ln(0.54, 0.40, 0.59, 0.405, 0.026, brow); break;
      case 2: ln(0.41, 0.392, 0.46, 0.381, 0.016, brow); ln(0.54, 0.381, 0.59, 0.392, 0.016, brow); break;
      case 3: ln(0.41, 0.393, 0.46, 0.412, 0.018, brow); ln(0.54, 0.412, 0.59, 0.393, 0.018, brow); break;
      case 4: ln(0.41, 0.405, 0.46, 0.40, 0.009, brow); ln(0.54, 0.40, 0.59, 0.405, 0.009, brow); break;
      default: ln(0.41, 0.405, 0.46, 0.40, 0.016, brow); ln(0.54, 0.40, 0.59, 0.405, 0.016, brow);
    }

    // Eyes
    const happy = (cx) => arc(cx, 0.48, 0.028, 0.014, PI + 0.35, 2 * PI - 0.35, INK);
    const iris = v[9] ? eye : INK;
    switch (v[3]) {
      case 1: happy(0.43); happy(0.57); break;
      case 2: ln(0.405, 0.47, 0.455, 0.47, 0.014, INK); ln(0.545, 0.47, 0.595, 0.47, 0.014, INK); break;
      case 3:
        circ(0.43, 0.465, 0.034, 0xFFFFFF); circ(0.57, 0.465, 0.034, 0xFFFFFF);
        circ(0.43, 0.469, 0.019, iris); circ(0.57, 0.469, 0.019, iris); break;
      case 4: circ(0.43, 0.465, 0.022, dot); happy(0.57); break;
      case 5: clipped(0.457, false, () => { circ(0.43, 0.468, 0.024, dot); circ(0.57, 0.468, 0.024, dot); }); break;
      case 6:
        circ(0.43, 0.465, 0.03, iris); circ(0.57, 0.465, 0.03, iris);
        circ(0.438, 0.455, 0.0095, 0xFFFFFF); circ(0.578, 0.455, 0.0095, 0xFFFFFF); break;
      case 7: ell(0.43, 0.468, 0.027, 0.013, dot); ell(0.57, 0.468, 0.027, 0.013, dot); break;
      default: circ(0.43, 0.465, 0.022, dot); circ(0.57, 0.465, 0.022, dot);
    }
    circ(0.5, 0.52, 0.012, shade(skin, -18));

    // Facial hair
    switch (v[11]) {
      case 1: clipped(0.535, false, () => ellA(0.5, 0.585, 0.165, 0.085, shade(skin, -38), 105)); break;
      case 2: ell(0.468, 0.538, 0.034, 0.013, hair); ell(0.532, 0.538, 0.034, 0.013, hair); break;
      case 3:
        clipped(0.53, false, () => ell(0.5, 0.585, 0.19, 0.105, hair));
        ell(0.468, 0.538, 0.034, 0.013, hair); ell(0.532, 0.538, 0.034, 0.013, hair); break;
      case 4:
        ell(0.5, 0.632, 0.032, 0.03, hair);
        ell(0.468, 0.538, 0.03, 0.011, hair); ell(0.532, 0.538, 0.03, 0.011, hair); break;
    }

    // Mouth
    switch (v[4]) {
      case 1:
        clipped(0.555, false, () => circ(0.5, 0.555, 0.05, MOUTH));
        rr(0.465, 0.555, 0.07, 0.014, 0.004, 0xFFFFFF); break;
      case 2: ln(0.465, 0.57, 0.535, 0.57, 0.014, MOUTH); break;
      case 3: ell(0.5, 0.572, 0.022, 0.028, MOUTH); break;
      case 4: arc(0.52, 0.55, 0.04, 0.014, 0.45, 1.9, MOUTH); break;
      case 5:
        clipped(0.55, false, () => { circ(0.5, 0.55, 0.062, MOUTH); circ(0.5, 0.605, 0.03, 0xE0707A); });
        rr(0.458, 0.55, 0.084, 0.014, 0.004, 0xFFFFFF); break;
      case 6: ell(0.515, 0.585, 0.018, 0.016, 0xE0707A); arc(0.5, 0.55, 0.045, 0.014, 0.35, PI - 0.35, MOUTH); break;
      case 7: arc(0.5, 0.605, 0.042, 0.014, PI + 0.4, 2 * PI - 0.4, MOUTH); break;
      case 8: rr(0.458, 0.548, 0.084, 0.04, 0.018, MOUTH); rr(0.466, 0.554, 0.068, 0.028, 0.012, 0xFFFFFF); break;
      case 9:
        arc(0.48, 0.553, 0.02, 0.012, 0.25, PI - 0.25, MOUTH);
        arc(0.52, 0.553, 0.02, 0.012, 0.25, PI - 0.25, MOUTH); break;
      default: arc(0.5, 0.55, 0.045, 0.014, 0.35, PI - 0.35, MOUTH);
    }

    // Accessories
    switch (acc) {
      case 1:
        ring(0.43, 0.465, 0.05, 0.012, 0x23262E); ring(0.57, 0.465, 0.05, 0.012, 0x23262E);
        ln(0.48, 0.462, 0.52, 0.462, 0.012, 0x23262E);
        ln(0.38, 0.46, 0.31, 0.45, 0.012, 0x23262E); ln(0.62, 0.46, 0.69, 0.45, 0.012, 0x23262E); break;
      case 2:
        rr(0.375, 0.44, 0.11, 0.055, 0.02, 0x15151A); rr(0.515, 0.44, 0.11, 0.055, 0.02, 0x15151A);
        ln(0.48, 0.455, 0.52, 0.455, 0.014, 0x15151A); break;
      case 3:
        arc(0.5, 0.45, 0.235, 0.035, PI + 0.15, 2 * PI - 0.15, 0x2B2F3A);
        rr(0.255, 0.40, 0.075, 0.14, 0.03, 0x2B2F3A); rr(0.67, 0.40, 0.075, 0.14, 0.03, 0x2B2F3A);
        rr(0.268, 0.425, 0.05, 0.09, 0.02, top); rr(0.682, 0.425, 0.05, 0.09, 0.02, top); break;
      case 4:
        clipped(0.365, true, () => ell(0.5, 0.35, 0.215, 0.16, top));
        rr(0.33, 0.335, 0.42, 0.045, 0.02, topDark); break;
      case 5:
        rr(0.29, 0.17, 0.42, 0.21, 0.10, top);
        rr(0.285, 0.32, 0.43, 0.06, 0.03, topDark); break;
      case 6: rr(0.305, 0.325, 0.39, 0.038, 0.019, top); break;
      case 7:
        rr(0.375, 0.235, 0.25, 0.055, 0.01, GOLD);
        tri(0.375, 0.24, 0.425, 0.24, 0.385, 0.165, GOLD);
        tri(0.47, 0.24, 0.53, 0.24, 0.50, 0.15, GOLD);
        tri(0.575, 0.24, 0.625, 0.24, 0.615, 0.165, GOLD);
        circ(0.50, 0.262, 0.012, 0xE04A3F); break;
      case 8:
        tri(0.60, 0.245, 0.60, 0.325, 0.665, 0.285, top);
        tri(0.73, 0.245, 0.73, 0.325, 0.665, 0.285, top);
        circ(0.665, 0.285, 0.02, topDark); break;
      case 9: circ(0.302, 0.518, 0.013, GOLD); circ(0.698, 0.518, 0.013, GOLD); break;
      case 10:
        ring(0.43, 0.465, 0.046, 0.008, 0xB8902C); ring(0.57, 0.465, 0.046, 0.008, 0xB8902C);
        ln(0.476, 0.46, 0.524, 0.46, 0.008, 0xB8902C);
        ln(0.384, 0.46, 0.31, 0.452, 0.008, 0xB8902C); ln(0.616, 0.46, 0.69, 0.452, 0.008, 0xB8902C); break;
    }
    ctx.restore();
  }

  /* A canvas element with the avatar drawn at device resolution. */
  function canvas(code, cssSize) {
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    const c = document.createElement("canvas");
    c.width = c.height = Math.round(cssSize * dpr);
    c.style.width = c.style.height = cssSize + "px";
    c.className = "avatar";
    c.setAttribute("role", "img");
    draw(c.getContext("2d"), code, 0, 0, c.width);
    return c;
  }

  return { draw, canvas, parse, valid, COUNTS };
})();
