.pragma library

// The emoji page, by category. Each category's icon is its first emoji.
const categories = [
    {name: "Smileys", list: "😀 😃 😄 😁 😆 😅 🤣 😂 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😗 😚 😋 😛 😜 🤪 😝 🤑 🤗 🤭 🤫 🤔 🤐 🤨 😐 😑 😶 😏 😒 🙄 😬 😌 😔 😪 😴 😷 🤒 🤕 🤢 🤮 🥵 🥶 🥴 😵 🤯 🤠 🥳 😎 🤓 🧐 😕 😟 🙁 😮 😯 😲 😳 🥺 😦 😧 😨 😰 😥 😢 😭 😱 😖 😣 😞 😓 😩 😫 🥱 😤 😡 😠 🤬 😈 👿 💀 💩 🤡 👻 👽 🤖"},
    {name: "People", list: "👋 🤚 🖐️ ✋ 🖖 👌 🤌 🤏 ✌️ 🤞 🤟 🤘 🤙 👈 👉 👆 👇 ☝️ 👍 👎 ✊ 👊 🤛 🤜 👏 🙌 👐 🤲 🤝 🙏 ✍️ 💪 👀 👅 👄 🧠 👶 🧒 👦 👧 🧑 👱 👨 👩 🧓 👴 👵 🙋 🙅 🙆 🤷 🤦 🙇 💁 🧑‍💻 👩‍💻 👨‍💻 🧑‍🍳 🧑‍🚀 🥷 🦸 🧙 🧛 🧜 🧚 💃 🕺 🚶 🏃 🧘"},
    {name: "Nature", list: "🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯 🦁 🐮 🐷 🐸 🐵 🐔 🐧 🐦 🐤 🦆 🦅 🦉 🦇 🐺 🐗 🐴 🦄 🐝 🐛 🦋 🐌 🐞 🐢 🐍 🦎 🐙 🦑 🦀 🐠 🐟 🐬 🐳 🐋 🦈 🐊 🌵 🎄 🌲 🌳 🌴 🌱 🌿 ☘️ 🍀 🍁 🍂 🍃 🌷 🌹 🌺 🌸 🌼 🌻 🌞 🌝 🌛 🌚 🌕 🌙 ⭐ 🌟 ✨ ⚡ 🔥 🌈 ☀️ ⛅ ☁️ 🌧️ ⛈️ ❄️ ☃️ 🌊 💧"},
    {name: "Food", list: "🍏 🍎 🍐 🍊 🍋 🍌 🍉 🍇 🍓 🫐 🍈 🍒 🍑 🥭 🍍 🥥 🥝 🍅 🍆 🥑 🥦 🥬 🥒 🌶️ 🌽 🥕 🧄 🧅 🥔 🥐 🥯 🍞 🥖 🥨 🧀 🥚 🍳 🧈 🥞 🧇 🥓 🥩 🍗 🍖 🌭 🍔 🍟 🍕 🥪 🌮 🌯 🥗 🍝 🍜 🍲 🍛 🍣 🍱 🥟 🍤 🍙 🍚 🍦 🍰 🎂 🧁 🍫 🍬 🍭 🍩 🍪 ☕ 🍵 🧃 🥤 🍺 🍻 🥂 🍷 🥃 🍸 🍹"},
    {name: "Activities", list: "⚽ 🏀 🏈 ⚾ 🎾 🏐 🏉 🎱 🏓 🏸 🥊 ⛳ 🎣 🎿 🏂 🏆 🥇 🥈 🥉 🎮 🕹️ 🎲 🧩 🎯 🎳 🎸 🎹 🎺 🎻 🥁 🎤 🎧 🎬 🎨 🎁 🎈 🎉 🎊"},
    {name: "Travel", list: "🚗 🚕 🚙 🚌 🏎️ 🚓 🚑 🚒 🚚 🚜 🛵 🏍️ 🚲 🛴 🚨 🚄 🚆 🚇 🚊 ✈️ 🛫 🛬 🚀 🛸 🚁 ⛵ 🚤 🛳️ ⚓ 🚧 ⛽ 🗺️ 🗽 🗼 🏰 🏟️ 🎡 🎢 ⛲ 🏖️ 🏝️ 🏜️ 🌋 ⛰️ 🏔️ 🏕️ 🏠 🏡 🏢 🏥 🏦 🏨 🏫 ⛪ 🌅 🌄 🌠 🎆 🌇 🌆 🏙️ 🌃 🌌 🌉"},
    {name: "Objects", list: "💻 🖥️ ⌨️ 🖱️ 📱 ☎️ 🔋 🔌 💡 🔦 🕯️ 📷 📸 📹 📺 📻 ⏰ ⌛ 📚 📖 📝 ✏️ 🖊️ 📎 ✂️ 📌 📅 📈 📉 📊 📦 ✉️ 📧 🔒 🔓 🔑 🔨 🛠️ ⚙️ 🧲 💊 💉 🧪 🔭 🔬 💰 💳 💎 🏷️ 🛒"},
    {name: "Symbols", list: "❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❣️ 💕 💞 💓 💗 💖 💘 💝 ✅ ☑️ ✔️ ❌ ❎ ➕ ➖ ➗ ✖️ ♾️ ‼️ ⁉️ ❓ ❗ 💯 🚫 ⛔ ⭕ 🛑 ♻️ ⚠️ 🔆 🆗 🆕 🆒 🆓 🔝 🔜 ▶️ ⏸️ ⏹️ ⏭️ ⏮️ 🔀 🔁 ➡️ ⬅️ ⬆️ ⬇️ ↗️ ↘️ 🔄 🎵 🎶 ©️ ®️ ™️ 🔴 🟠 🟡 🟢 🔵 🟣 ⚫ ⚪ 🟥 🟧 🟨 🟩 🟦 🟪 ⬛ ⬜"},
    {name: "Flags", list: "🏁 🚩 🏳️ 🏴 🏳️‍🌈 🇩🇪 🇦🇹 🇨🇭 🇪🇺 🇺🇸 🇬🇧 🇫🇷 🇪🇸 🇮🇹 🇳🇱 🇧🇪 🇩🇰 🇸🇪 🇳🇴 🇫🇮 🇵🇱 🇨🇿 🇵🇹 🇮🇪 🇬🇷 🇺🇦 🇹🇷 🇨🇦 🇲🇽 🇧🇷 🇦🇷 🇯🇵 🇰🇷 🇨🇳 🇮🇳 🇦🇺 🇳🇿 🇿🇦"},
]

// The flat list the grid shows, recent emoji first, with where each category starts.
function build(recent) {
    const items = []
    const sections = []
    if (recent && recent.length > 0) {
        sections.push({name: "Recent", icon: "🕘", start: 0})
        for (const e of recent)
            items.push(e)
    }
    for (const category of categories) {
        const list = category.list.split(" ")
        sections.push({name: category.name, icon: list[0], start: items.length})
        for (const e of list)
            items.push(e)
    }
    return {items: items, sections: sections}
}
