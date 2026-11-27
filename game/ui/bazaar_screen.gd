# Stage 12: a bazaar terminal (shared_terminal_bazaar.iff), in Battlefront
# II's frame. Left: everyone's listings, searched, cheapest first, with price
# trends for a Merchant with Market Analytics. Right: your listings, and what
# you carry to list at a price (a Merchant's shop slots and fees apply).
extends "res://ui/bf2_screen.gd"

signal request(action: int, id: String, target: int, position: Vector3, amount: int, item_id: int, text: String)
signal closed

const ItemTooltip = preload("res://ui/item_tooltip.gd")
const ItemGrade = preload("res://ui/item_grade.gd")

const BROWSE := 15
const LIST := 16
const BUY := 17
const CANCEL := 18
const ROWS := 10

var title := "Galactic Bazaar"
var spec := {}           # SwmClient.get_specialist()
var items: Array = []    # what you carry
var equipped := {}       # item id -> true, for what's in a loadout (can't be listed)
var catalog: Array = []
var credits := 0
var merchant_level := 0
var feedback := ""

var _scroll := 0
var _scroll_right := 0
var _selected := -1
var _search: LineEdit
var _price: LineEdit
var _defs := {}


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	_search = LineEdit.new()
	_search.placeholder_text = "Search: name, shop, seller"
	_search.text_submitted.connect(func(_t: String) -> void: _browse())
	add_child(_search)
	_price = LineEdit.new()
	_price.placeholder_text = "Price (credits)"
	add_child(_price)


func open_bazaar(terminal_name: String) -> void:
	title = terminal_name
	feedback = ""
	_selected = -1
	visible = true
	_browse()
	queue_redraw()


func _browse() -> void:
	request.emit(BROWSE, "", 0, Vector3.ZERO, 0, 0, _search.text.strip_edges())


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var step := -1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1
		if (event as InputEventMouseButton).position.x < size.x * 0.5:
			_scroll = maxi(0, _scroll + step)
		else:
			_scroll_right = maxi(0, _scroll_right + step)
		queue_redraw()
		accept_event()
		return
	super._gui_input(event)


func _name(def_id: String, custom := "", mark := 0) -> String:
	if custom != "":
		return custom
	if _defs.size() != catalog.size():
		_defs.clear()
		for d: Dictionary in catalog:
			_defs[str(d.get("id", ""))] = d
	return ItemTooltip.item_name(_defs.get(def_id, {"name": def_id}), {"mark": mark})


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(900, 520, Vector2(0, 12))
	text("gamefont_large", title, Vector2(size.x * 0.5, box.position.y - 44 * u), "Center", ACCENT)
	draw_rect(box, Color(0.04, 0.05, 0.07, 0.92))
	frame(box)
	button(Rect2(box.end.x - 104 * u, box.position.y + 10 * u, 88 * u, 26 * u), "Close", "close", false, true)
	var half := (box.size.x - 54 * u) * 0.5
	var lx := box.position.x + 18 * u
	var rx := lx + half + 18 * u
	var y := box.position.y + 44 * u
	# Search.
	_search.position = Vector2(lx, y)
	_search.size = Vector2(half - 110 * u, 26 * u)
	button(Rect2(lx + half - 100 * u, y, 100 * u, 26 * u), "Search", "search")
	text("gamefont_small", "%d Cr" % credits, Vector2(rx, y + 2 * u), "Left", TEXT)
	var limit := 5 + clampi(merchant_level, 0, 100) / 5
	var fee := 6.0 - 4.0 * clampi(merchant_level, 0, 100) / 100.0
	text("gamefont_tiny", "Merchant %d: %d shop slots, %.1f%% fee on sales" % [merchant_level, limit, fee],
			Vector2(rx + half, y + 5 * u), "Right", DIM, 0.9)
	y += 36 * u
	# Everyone's listings.
	var listings: Array = spec.get("bazaar", [])
	var prices := {}
	for p: Dictionary in spec.get("prices", []):
		prices[str(p["def_id"])] = p
	_scroll = clampi(_scroll, 0, maxi(0, listings.size() - ROWS))
	if listings.is_empty():
		text("gamefont_small", "Nothing listed that matches.", Vector2(lx + half * 0.5, y + 40 * u), "Center", DIM)
	for i in mini(ROWS, listings.size() - _scroll):
		var l: Dictionary = listings[_scroll + i]
		var rect := Rect2(lx, y + i * 38 * u, half, 36 * u)
		draw_rect(rect, Color(0.18, 0.22, 0.3, 0.5) if _hover == "buy:%d" % int(l["id"]) else Color(0.1, 0.13, 0.18, 0.4))
		var ldef: Dictionary = _defs.get(str(l["def_id"]), {})
		var lgrade := ItemGrade.label(ldef, l)
		text("gamefont_tiny", "%s%s" % [_name(str(l["def_id"]), str(l.get("custom_name", "")), int(l.get("mark", 0))),
				"  (" + lgrade + ")" if lgrade != "" else ""],
				Vector2(rect.position.x + 8 * u, rect.position.y + 3 * u), "Left", ItemGrade.color(ldef, l))
		var who := str(l.get("shop", "")) if str(l.get("shop", "")) != "" else str(l.get("seller", ""))
		var trend := ""
		if prices.has(str(l["def_id"])):
			var p: Dictionary = prices[str(l["def_id"])]
			trend = "  ·  sold %d, avg %d (%d-%d)" % [int(p["count"]), int(p["average"]), int(p["low"]), int(p["high"])] \
					if int(p["count"]) > 0 else "  ·  no sales yet"
		text("gamefont_tiny", who + trend, Vector2(rect.position.x + 8 * u, rect.position.y + 20 * u), "Left", DIM, 0.85)
		var affordable := credits >= int(l["price"])
		button(Rect2(rect.end.x - 120 * u, rect.position.y + 5 * u, 114 * u, 26 * u), "%d Cr" % int(l["price"]),
				"buy:%d" % int(l["id"]), false, false, affordable)
	# Your listings and what you carry.
	var ry := box.position.y + 80 * u
	var mine: Array = spec.get("my_listings", [])
	text("gamefont_small", "Your listings (%d of %d)" % [mine.size(), limit], Vector2(rx, ry), "Left", TEXT)
	ry += 24 * u
	for l: Dictionary in mine:
		if ry > box.position.y + 230 * u:
			break
		var rect := Rect2(rx, ry, half, 28 * u)
		draw_rect(rect, Color(0.1, 0.13, 0.18, 0.4))
		text("gamefont_tiny", "%s  ·  %d Cr" % [_name(str(l["def_id"]), str(l.get("custom_name", "")), int(l.get("mark", 0))), int(l["price"])],
				Vector2(rect.position.x + 8 * u, ry + 7 * u), "Left", TEXT)
		button(Rect2(rect.end.x - 96 * u, ry + 1 * u, 90 * u, 26 * u), "Take back", "cancel:%d" % int(l["id"]))
		ry += 30 * u
	ry = box.position.y + 240 * u
	text("gamefont_small", "List something", Vector2(rx, ry), "Left", TEXT)
	ry += 24 * u
	var listable: Array = []
	for item: Dictionary in items:
		if not equipped.has(int(item["id"])):
			listable.append(item)
	_scroll_right = clampi(_scroll_right, 0, maxi(0, listable.size() - 5))
	for i in mini(5, listable.size() - _scroll_right):
		var item: Dictionary = listable[_scroll_right + i]
		var rect := Rect2(rx, ry, half, 26 * u)
		var iid := int(item["id"])
		draw_rect(rect, Color(0.2, 0.25, 0.35, 0.6) if iid == _selected or _hover == "pick:%d" % iid else Color(0.1, 0.13, 0.18, 0.4))
		text("gamefont_tiny", _name(str(item["def_id"]), str(item.get("custom_name", "")), int(item.get("mark", 0))), Vector2(rect.position.x + 8 * u, ry + 6 * u),
				"Left", ACCENT if iid == _selected else TEXT)
		region(rect, "pick:%d" % iid)
		ry += 28 * u
	_price.position = Vector2(rx, box.end.y - 70 * u)
	_price.size = Vector2(half - 130 * u, 26 * u)
	button(Rect2(rx + half - 120 * u, box.end.y - 70 * u, 120 * u, 26 * u), "List it", "list", false, false, _selected >= 0)
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 30 * u), "Center", ACCENT)


func _on_click(id: String, _at: Vector2) -> void:
	feedback = ""
	if id == "close":
		closed.emit()
	elif id == "search":
		_browse()
	elif id.begins_with("buy:"):
		request.emit(BUY, "", 0, Vector3.ZERO, int(id.substr(4)), 0, "")
	elif id.begins_with("cancel:"):
		request.emit(CANCEL, "", 0, Vector3.ZERO, int(id.substr(7)), 0, "")
	elif id.begins_with("pick:"):
		_selected = int(id.substr(5))
	elif id == "list":
		var price := int(_price.text.strip_edges())
		if price <= 0:
			feedback = "Set a price first"
		else:
			request.emit(LIST, "", 0, Vector3.ZERO, price, _selected, "")
			_selected = -1
	queue_redraw()
