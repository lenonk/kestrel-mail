.pragma library

function hash32(s, seed) {
    let h = (seed === undefined ? 2166136261 : seed) >>> 0
    const t = (s || "").toString()
    for (let i = 0; i < t.length; ++i) {
        h ^= (t.charCodeAt(i) & 0xff)
        h = Math.imul(h, 16777619) >>> 0
    }
    return h >>> 0
}

function hsvToHex(hue, sat, val) {
    const s = sat / 255.0
    const v = val / 255.0
    const c = v * s
    const hp = hue / 60.0
    const x = c * (1 - Math.abs((hp % 2) - 1))
    let r1 = 0, g1 = 0, b1 = 0
    if (hp >= 0 && hp < 1) { r1 = c; g1 = x; b1 = 0 }
    else if (hp < 2) { r1 = x; g1 = c; b1 = 0 }
    else if (hp < 3) { r1 = 0; g1 = c; b1 = x }
    else if (hp < 4) { r1 = 0; g1 = x; b1 = c }
    else if (hp < 5) { r1 = x; g1 = 0; b1 = c }
    else { r1 = c; g1 = 0; b1 = x }
    const m = v - c

    function h2(n) {
        const s2 = Math.max(0, Math.min(255, Math.round(n))).toString(16)
        return s2.length === 1 ? "0" + s2 : s2
    }

    return "#" + h2((r1 + m) * 255) + h2((g1 + m) * 255) + h2((b1 + m) * 255)
}

function hexToRgb(hex) {
    const s = (hex || "").toString().trim()
    if (s.length !== 7 || s[0] !== "#") return null
    return {
        r: parseInt(s.slice(1, 3), 16),
        g: parseInt(s.slice(3, 5), 16),
        b: parseInt(s.slice(5, 7), 16)
    }
}

function colorDistSq(a, b) {
    const ca = hexToRgb(a)
    const cb = hexToRgb(b)
    if (!ca || !cb) return 0
    const dr = ca.r - cb.r
    const dg = ca.g - cb.g
    const db = ca.b - cb.b
    return dr * dr + dg * dg + db * db
}

function candidateTagColor(name, attempt) {
    const lower = (name || "").toString().toLowerCase()
    const mixed = hash32(lower + "#" + String(attempt || 0), hash32(lower, 2166136261))
    let hue = mixed % 360
    if (hue >= 42 && hue <= 62) hue = (hue + 37) % 360
    const satBuckets = [170, 190, 210, 230, 245]
    const valBuckets = [200, 218, 235, 250]
    const sat = satBuckets[(mixed >>> 8) % satBuckets.length]
    const val = valBuckets[(mixed >>> 11) % valBuckets.length]
    return hsvToHex(hue, sat, val)
}

function tagColorForName(name, usedColors, importantYellow) {
    const lower = (name || "").toString().toLowerCase()
    if (lower === "important") return importantYellow

    const used = usedColors || []
    const minDistSq = 9500
    let best = candidateTagColor(lower, 0)
    let bestScore = -1

    for (let attempt = 0; attempt < 80; ++attempt) {
        const cand = candidateTagColor(lower, attempt)
        let minSeen = 1e9
        for (let i = 0; i < used.length; ++i)
            minSeen = Math.min(minSeen, colorDistSq(cand, used[i]))

        if (used.length === 0 || minSeen >= minDistSq)
            return cand

        if (minSeen > bestScore) {
            bestScore = minSeen
            best = cand
        }
    }
    return best
}
