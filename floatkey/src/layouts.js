.pragma library

// Keyboard layouts, as rows of keys. In a row:
//   "q"               a character key; shift types its upper case
//   {c: "1", s: "!"}  a character key with its own shifted character, which the
//                     full keyboard shows above it and types on a downward
//                     flick, as on a 13" iPad
//   {k: "shift"}      a special key (KeyboardView.qml says what each does)
//   w: 1.5            a key's width in key units (1 by default)
//   {gap: 0.5}        empty space; a touch there goes to the nearest key
//   {stack: [a, b]}   two half-height keys in one slot, like a Mac's up and
//                     down arrows
//
// Every layout has the full keyboard the docked mode shows, and the compact
// one (letters, numbers, symbols) the floating and corner modes show, the size
// and shape of an iPhone's.

const fullBottom = [
    {k: "ctrl", w: 1.25}, {k: "alt"}, {k: "super"}, {k: "esc"}, {k: "globe"},
    {k: "space", w: 5},
    {k: "left"}, {stack: [{k: "up"}, {k: "down"}]}, {k: "right"}, {k: "hide", w: 1.25},
]

function compactBottom(units) {
    // The return key takes what the 123, globe and space keys leave.
    return [{k: "numbers", w: 1.3}, {k: "globe", w: 1.2}, {k: "space", w: 5}, {k: "return", w: units - 7.5}]
}

function pageBottom(back, units) {
    return [{k: back, w: 1.3}, {k: "globe", w: 1.2}, {k: "space", w: 5}, {k: "return", w: units - 7.5}]
}

const punctuation = [
    {k: "symbols", w: 1.3}, {gap: 0.2},
    {c: ".", w: 1.4}, {c: ",", w: 1.4}, {c: "?", w: 1.4}, {c: "!", w: 1.4}, {c: "'", w: 1.4},
    {gap: 0.2}, {k: "backspace", w: 1.3},
]

// The symbols page trades the punctuation for the keys a terminal wants.
const modifiers = [
    {k: "numbers", w: 1.3}, {gap: 0.2},
    {k: "ctrl", w: 1.4}, {k: "alt", w: 1.4}, {k: "super", w: 1.4}, {k: "esc", w: 1.4}, {k: "tab", w: 1.4},
    {gap: 0.2}, {k: "backspace", w: 1.3},
]

const latinAlternates = {
    "a": "àáâäæãåā", "c": "çćč", "e": "èéêëēėę", "i": "îïíīįì", "l": "ł", "n": "ñń",
    "o": "ôöòóœøōõ", "s": "ßśš", "u": "ûüùúū", "y": "ÿ", "z": "žźż",
    "-": "–—•", "$": "€£¥₩¢", "€": "$£¥₩¢", "&": "§", ".": "…", "?": "¿", "!": "¡",
    "'": "’‘‚`", "\"": "“”„«»", "0": "°", "%": "‰", "/": "\\", "=": "≠≈", "<": "≤«", ">": "≥»",
    "1": "¹", "2": "²", "3": "³",
}

const layouts = {
    us: {
        name: "English (US)",
        full: {
            letters: [
                [{c: "`", s: "~"}, {c: "1", s: "!"}, {c: "2", s: "@"}, {c: "3", s: "#"}, {c: "4", s: "$"},
                 {c: "5", s: "%"}, {c: "6", s: "^"}, {c: "7", s: "&"}, {c: "8", s: "*"}, {c: "9", s: "("},
                 {c: "0", s: ")"}, {c: "-", s: "_"}, {c: "=", s: "+"}, {k: "backspace", w: 1.5}],
                [{k: "tab", w: 1.5}, "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
                 {c: "[", s: "{"}, {c: "]", s: "}"}, {c: "\\", s: "|"}],
                [{k: "caps", w: 1.75}, "a", "s", "d", "f", "g", "h", "j", "k", "l",
                 {c: ";", s: ":"}, {c: "'", s: "\""}, {k: "return", w: 1.75}],
                [{k: "shift", w: 2.25}, "z", "x", "c", "v", "b", "n", "m",
                 {c: ",", s: "<"}, {c: ".", s: ">"}, {c: "/", s: "?"}, {k: "shift", w: 2.25}],
                fullBottom,
            ],
        },
        compact: {
            letters: [
                ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
                [{gap: 0.5}, "a", "s", "d", "f", "g", "h", "j", "k", "l", {gap: 0.5}],
                [{k: "shift", w: 1.3}, {gap: 0.2}, "z", "x", "c", "v", "b", "n", "m", {gap: 0.2},
                 {k: "backspace", w: 1.3}],
                compactBottom(10),
            ],
            numbers: [
                ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
                ["-", "/", ":", ";", "(", ")", "$", "&", "@", "\""],
                punctuation,
                pageBottom("letters", 10),
            ],
            symbols: [
                ["[", "]", "{", "}", "#", "%", "^", "*", "+", "="],
                ["_", "\\", "|", "~", "<", ">", "€", "£", "¥", "•"],
                modifiers,
                pageBottom("letters", 10),
            ],
        },
        alternates: latinAlternates,
    },

    de: {
        name: "Deutsch",
        full: {
            letters: [
                [{c: "^", s: "°"}, {c: "1", s: "!"}, {c: "2", s: "\""}, {c: "3", s: "§"}, {c: "4", s: "$"},
                 {c: "5", s: "%"}, {c: "6", s: "&"}, {c: "7", s: "/"}, {c: "8", s: "("}, {c: "9", s: ")"},
                 {c: "0", s: "="}, {c: "ß", s: "?"}, {c: "´", s: "`"}, {k: "backspace", w: 1.5}],
                [{k: "tab", w: 1.5}, "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü",
                 {c: "+", s: "*"}, {c: "#", s: "'"}],
                [{k: "caps", w: 1.75}, "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä",
                 {k: "return", w: 1.75}],
                [{k: "shift", w: 1.25}, {c: "<", s: ">"}, "y", "x", "c", "v", "b", "n", "m",
                 {c: ",", s: ";"}, {c: ".", s: ":"}, {c: "-", s: "_"}, {k: "shift", w: 2.25}],
                fullBottom,
            ],
        },
        compact: {
            letters: [
                ["q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü"],
                ["a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä"],
                [{k: "shift", w: 1.75}, {gap: 0.25}, "y", "x", "c", "v", "b", "n", "m", {gap: 0.25},
                 {k: "backspace", w: 1.75}],
                compactBottom(11),
            ],
            numbers: [
                ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
                ["-", "/", ":", ";", "(", ")", "€", "&", "@", "\""],
                punctuation,
                pageBottom("letters", 10),
            ],
            symbols: [
                ["[", "]", "{", "}", "#", "%", "^", "*", "+", "="],
                ["_", "\\", "|", "~", "<", ">", "$", "£", "¥", "•"],
                modifiers,
                pageBottom("letters", 10),
            ],
        },
        // What a German PC keyboard puts behind AltGr comes up on a long press too.
        alternates: Object.assign({}, latinAlternates, {
            "q": "@", "e": "€èéêëēėę", "m": "µ", "+": "~", "<": "|≤«", "7": "{", "8": "[", "9": "]",
            "0": "}°", "ß": "\\ẞ", "2": "²", "3": "³", "a": "äàáâæãåā", "o": "öôòóœøōõ", "u": "üûùúū",
        }),
    },
}

function layout(code) {
    return layouts[code] || layouts.us
}

function name(code) {
    return layout(code).name
}

function normalize(entry) {
    if (typeof entry === "string")
        return {kind: "char", c: entry, w: 1}
    if (entry.gap !== undefined)
        return {kind: "gap", w: entry.gap}
    if (entry.stack)
        return {kind: "stack", w: entry.w || 1, keys: entry.stack.map(normalize)}
    if (entry.k)
        return {kind: entry.k, w: entry.w || 1}
    return {kind: "char", c: entry.c, s: entry.s, w: entry.w || 1}
}

function rowUnits(row) {
    let units = 0
    for (const entry of row)
        units += normalize(entry).w
    return units
}

// A page's keys with their place in key units: `ux` from the left, `row` from
// the top (a fraction for stacked keys), `h` in rows. Rows narrower than the
// widest are centred.
function keys(rows) {
    if (!rows || rows.length === 0)
        return {units: 1, rows: 1, keys: []}
    let units = 0
    for (const row of rows)
        units = Math.max(units, rowUnits(row))
    const out = []
    rows.forEach(function (row, r) {
        let x = (units - rowUnits(row)) / 2
        for (const entry of row) {
            const key = normalize(entry)
            if (key.kind === "stack") {
                key.keys.forEach(function (part, i) {
                    part.w = key.w
                    part.ux = x
                    part.row = r + i / key.keys.length
                    part.h = 1 / key.keys.length
                    out.push(part)
                })
            } else if (key.kind !== "gap") {
                key.ux = x
                key.row = r
                key.h = 1
                out.push(key)
            }
            x += key.w
        }
    })
    return {units: units, rows: rows.length, keys: out}
}

function isLetter(ch) {
    return !!ch && ch.toLowerCase() !== ch.toUpperCase()
}

function upper(ch) {
    if (ch === "ß")
        return "ẞ"
    const u = ch.toUpperCase()
    return Array.from(u).length === 1 ? u : ch
}

// The long-press choices for a key, in the case shift asks for.
function alternates(code, key, shifted) {
    const table = layout(code).alternates || {}
    let options = (table[key.c] || "") + (key.s !== undefined ? (table[key.s] || "") : "")
    const out = []
    for (const ch of Array.from(options)) {
        const option = shifted && isLetter(key.c) ? upper(ch) : ch
        if (out.indexOf(option) < 0)
            out.push(option)
    }
    return out
}

// Every character the layouts can type, those on the keys of the given
// layouts first, so the keymap carries them from the start and the ones used
// most get the keycodes every app can receive.
function characters(codes) {
    const order = []
    const seen = {}
    function add(ch) {
        if (ch && !seen[ch]) {
            seen[ch] = true
            order.push(ch)
        }
    }
    const all = (codes || []).concat(Object.keys(layouts))
    for (const pass of ["keys", "alternates"]) {
        for (const code of all) {
            const l = layout(code)
            for (const rows of [l.full.letters, l.compact.letters, l.compact.numbers, l.compact.symbols]) {
                for (const row of rows) {
                    for (const entry of row) {
                        const k = normalize(entry)
                        if (k.kind !== "char")
                            continue
                        if (pass === "keys") {
                            add(k.c)
                            add(k.s)
                            if (isLetter(k.c))
                                add(upper(k.c))
                        } else {
                            alternates(code, k, false).concat(alternates(code, k, true)).forEach(add)
                        }
                    }
                }
            }
        }
    }
    return order.join("")
}
