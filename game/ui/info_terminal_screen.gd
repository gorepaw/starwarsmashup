# NewsNet Dev Board, Galactic Bazaar (Auction House Stub), Lucky Despot
# Gambling Stub, and City Data Terminal Screen in Battlefront II's UI style.
extends "res://ui/bf2_screen.gd"

signal closed

var mode := "data_terminal"  # "newsnet", "bazaar", "gambling", "data_terminal"
var title := "Terminal"
var detail := ""

var _articles: Array = []
var _selected := 0
var _bazaar_tab := "browse"
var _news_loaded := false


func open_terminal(p_mode: String, p_title: String, p_detail := "") -> void:
	mode = p_mode
	title = p_title
	detail = p_detail
	_selected = 0
	if mode == "newsnet":
		_load_news()
	visible = true
	queue_redraw()


func _load_news() -> void:
	_articles.clear()
	_news_loaded = true
	var file := FileAccess.open("res://hubs/news.ini", FileAccess.READ)
	if file == null:
		return
	var cur := {}
	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line.is_empty() or line.begins_with(";"):
			continue
		if line.begins_with("[") and line.ends_with("]"):
			if not cur.is_empty():
				_articles.append(cur)
			cur = {}
		elif "=" in line:
			var k := line.get_slice("=", 0).strip_edges()
			var v := line.get_slice("=", 1).strip_edges()
			cur[k] = v
	if not cur.is_empty():
		_articles.append(cur)


func _wrap(font: String, value: String, width: float) -> PackedStringArray:
	var lines := PackedStringArray()
	for paragraph in value.split("\\n"):
		for sub in paragraph.split("\n"):
			var line := ""
			for word in sub.split(" ", false):
				var candidate := word if line == "" else line + " " + word
				if line != "" and art.text_width(font, candidate, u) > width:
					lines.append(line)
					line = word
				else:
					line = candidate
			lines.append(line)
	return lines


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.5))
	var box := centered(740, 440, Vector2(0, 10))
	text("gamefont_large", title.to_upper(), Vector2(size.x * 0.5, box.position.y - 40 * u), "Center", ACCENT)
	frame(box)
	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	button(Rect2(box.end.x - 108 * u, y, 90 * u, 28 * u), "Close", "close", false, true)

	if mode == "newsnet":
		text("gamefont_medium", "Galactic NewsNet Dispatch", Vector2(x, y + 4 * u), "Left", TEXT)
		y += 38 * u
		var left := Rect2(x, y, 230 * u, box.end.y - y - 18 * u)
		var right := Rect2(left.end.x + 14 * u, y, box.end.x - left.end.x - 32 * u, left.size.y)
		draw_rect(left, Color(0.04, 0.06, 0.09, 0.6))
		draw_rect(right, Color(0.04, 0.06, 0.09, 0.6))
		if _articles.is_empty():
			text("gamefont_tiny", "No dispatches posted in game/hubs/news.ini.", Vector2(right.position.x + 12 * u, right.position.y + 14 * u), "Left", DIM)
			return
		_selected = clampi(_selected, 0, _articles.size() - 1)
		for i in mini(_articles.size(), 8):
			var art_item: Dictionary = _articles[i]
			var row := Rect2(left.position.x + 4 * u, left.position.y + 4 * u + i * 42 * u, left.size.x - 8 * u, 38 * u)
			draw_rect(row, Color(0.25, 0.32, 0.45, 0.55) if i == _selected else Color(0.1, 0.13, 0.18, 0.4))
			region(row, "article:%d" % i)
			text("gamefont_tiny", str(art_item.get("Title", "Dispatch")), row.position + Vector2(8, 4) * u, "Left", ACCENT if i == _selected else TEXT)
			text("gamefont_tiny", str(art_item.get("Date", "")), row.position + Vector2(8, 20) * u, "Left", DIM)
		var sel: Dictionary = _articles[_selected]
		var rx := right.position.x + 14 * u
		var ry := right.position.y + 12 * u
		text("gamefont_medium", str(sel.get("Title", "Dispatch")), Vector2(rx, ry), "Left", ACCENT)
		ry += 26 * u
		text("gamefont_tiny", "Posted %s  |  %s" % [str(sel.get("Date", "")), str(sel.get("Author", "Dispatch"))], Vector2(rx, ry), "Left", DIM)
		ry += 22 * u
		var lh: float = art.line_height("gamefont_tiny", u) + 3 * u
		for line: String in _wrap("gamefont_tiny", str(sel.get("Body", "")), right.size.x - 28 * u):
			text("gamefont_tiny", line, Vector2(rx, ry), "Left", TEXT)
			ry += lh
		return

	if mode == "bazaar":
		text("gamefont_medium", "Galactic Bazaar  (Auction House Stub)", Vector2(x, y + 4 * u), "Left", TEXT)
		y += 38 * u
		button(Rect2(x, y, 130 * u, 26 * u), "Browse Items", "btab:browse", _bazaar_tab == "browse")
		button(Rect2(x + 138 * u, y, 130 * u, 26 * u), "My Auctions", "btab:mine", _bazaar_tab == "mine")
		button(Rect2(x + 276 * u, y, 130 * u, 26 * u), "Create Listing", "btab:sell", _bazaar_tab == "sell")
		y += 38 * u
		var body := Rect2(x, y, box.size.x - 36 * u, box.end.y - y - 18 * u)
		draw_rect(body, Color(0.04, 0.06, 0.09, 0.6))
		var msg := "Galactic Commodities & Player Auction House coming in a later stage.\nBuy and sell crafted weapons, armor, speeders, and surveyed resources across cities."
		var by := body.position.y + 24 * u
		for line: String in _wrap("gamefont_small", msg, body.size.x - 36 * u):
			text("gamefont_small", line, Vector2(body.get_center().x, by), "Center", TEXT)
			by += art.line_height("gamefont_small", u) + 6 * u
		return

	# Gambling & generic city data terminal stub
	var subtitle := detail if detail != "" else ("Lucky Despot Gaming Machine (Stub)" if mode == "gambling" else "Mos Eisley Public Data Terminal (Stub)")
	text("gamefont_medium", subtitle, Vector2(x, y + 4 * u), "Left", TEXT)
	y += 46 * u
	var panel := Rect2(x, y, box.size.x - 36 * u, box.end.y - y - 18 * u)
	draw_rect(panel, Color(0.04, 0.06, 0.09, 0.6))
	var stub_text := "Place your credits at the Lucky Despot!\nSabacc tables, roulette wheels, and slot machines are currently offline for calibration and will open in a later update." \
			if mode == "gambling" else \
			"Public Civic Terminal.\nLocal directory services, bounties, and planetary datapads will populate this terminal in a future update."
	var py := panel.position.y + 36 * u
	for line: String in _wrap("gamefont_small", stub_text, panel.size.x - 36 * u):
		text("gamefont_small", line, Vector2(panel.get_center().x, py), "Center", TEXT)
		py += art.line_height("gamefont_small", u) + 6 * u


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("article:"):
		_selected = int(id.substr(8))
	elif id.begins_with("btab:"):
		_bazaar_tab = id.substr(5)
