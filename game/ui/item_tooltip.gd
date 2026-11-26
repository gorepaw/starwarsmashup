# Shared Battlefront II-styled item tooltip renderer for Inventory & Quartermaster:
# shows item name, kind/slot, faction, quality %, every stat, and color-coded
# deltas against the currently equipped item in the matching slot.
extends RefCounted

const Bf2Screen = preload("res://ui/bf2_screen.gd")
const GOOD := Color(0.45, 0.95, 0.5)
const BAD := Color(1.0, 0.4, 0.35)


static func stat_label(stat_name: String) -> String:
	match stat_name:
		"max_health": return "Max Health"
		"run_speed": return "Run Speed"
		"strafe_speed": return "Strafe Speed"
		"sprint_factor": return "Sprint Factor"
		"sprint_strafe": return "Sprint Strafe"
		"jump_height": return "Jump Height"
		"acceleration": return "Acceleration"
		"max_energy": return "Max Energy"
		"energy_restore": return "Energy Regen"
		"energy_restore_idle": return "Idle Energy Regen"
		"energy_drain_sprint": return "Sprint Drain"
		"mitigation": return "Armor Mitigation"
		"mitigation_ballistic": return "Ballistic Resist"
		"mitigation_laser": return "Laser Resist"
		"mitigation_electro": return "Electro Resist"
		"mitigation_bio": return "Bio Resist"
		"mitigation_explosive": return "Explosive Resist"
		"mitigation_melee": return "Melee Resist"
		"shield_capacity": return "Shield Capacity"
		"shield_recharge": return "Shield Recharge /s"
		"shield_delay": return "Shield Delay"
		"explosion_push_resist": return "Blast Knockback Resist"
	return stat_name.replace("_", " ").capitalize()


static func format_modifier(m: Dictionary) -> String:
	var raw_name := str(m.get("stat_name", ""))
	var sname := stat_label(raw_name)
	var add := float(m.get("add", 0.0))
	var mult := float(m.get("multiply", 1.0))
	var parts: PackedStringArray = []
	if absf(add) > 1e-4:
		if raw_name.begins_with("mitigation") or raw_name == "explosion_push_resist":
			parts.append("%s%d%%" % ["+" if add > 0 else "", roundi(add * 100.0)])
		elif raw_name == "shield_delay":
			parts.append("%s%.1fs" % ["+" if add > 0 else "", add])
		else:
			parts.append("%s%.0f" % ["+" if add > 0 else "", add])
	if absf(mult - 1.0) > 1e-4:
		var pct := roundi((mult - 1.0) * 100.0)
		parts.append("%s%d%%" % ["+" if pct > 0 else "", pct])
	return "%s: %s" % [sname, ", ".join(parts)] if not parts.is_empty() else sname


static func build_lines(def: Dictionary, inst: Dictionary, store: Object, equipped_def: Dictionary = {},
		equipped_inst: Dictionary = {}) -> Array:
	var lines: Array = []
	var def_id := str(def.get("id", ""))
	var quality := float(inst.get("quality", 0.5)) if not inst.is_empty() else 0.5
	var kind := str(def.get("kind", "weapon"))
	var slot := str(def.get("slot", ""))
	var faction := str(def.get("faction", "neutral")).capitalize()
	lines.append({"text": "%s | %s | %s" % [kind.replace("_", " ").capitalize(), slot.replace("_", " ").capitalize(), faction],
			"color": Bf2Screen.DIM})
	if not inst.is_empty() and kind != "junk" and kind != "supply" and kind != "quest" and kind != "ammo":
		lines.append({"text": "Quality: %d%%" % roundi(quality * 100.0), "color": Bf2Screen.ACCENT})
	if kind == "ammo":
		var ac := str(def.get("ammo_class", "standard")).capitalize()
		var cap := 99 if ac == "Heavy" else 999
		if not inst.is_empty():
			lines.append({"text": "%s Ammo: %d / %d rounds" % [ac, int(inst.get("quantity", 1)), cap], "color": Bf2Screen.ACCENT})
		else:
			lines.append({"text": "%s Ammo Pack: x%d (Max %d)" % [ac, int(def.get("ammo_batch", 100)), cap], "color": Bf2Screen.ACCENT})
		var dm := float(def.get("damage_mult", 1.0))
		var rm := float(def.get("rate_mult", 1.0))
		var hm := float(def.get("heat_mult", 1.0))
		var sm := float(def.get("spread_mult", 1.0))
		var dt_s := str(def.get("damage_type", ""))
		if dt_s != "":
			lines.append({"text": "Damage Type: %s" % dt_s.capitalize(), "color": Bf2Screen.TEXT})
		if absf(dm - 1.0) > 1e-3:
			lines.append({"text": "Shot Damage: %+d%%" % roundi((dm - 1.0) * 100.0), "color": GOOD if dm > 1.0 else BAD})
		if absf(rm - 1.0) > 1e-3:
			lines.append({"text": "Fire Rate: %+d%%" % roundi((rm - 1.0) * 100.0), "color": GOOD if rm > 1.0 else BAD})
		if absf(hm - 1.0) > 1e-3:
			lines.append({"text": "Heat / Shot: %+d%%" % roundi((hm - 1.0) * 100.0), "color": GOOD if hm < 1.0 else BAD})
		if absf(sm - 1.0) > 1e-3:
			lines.append({"text": "Spread: %+d%%" % roundi((sm - 1.0) * 100.0), "color": GOOD if sm < 1.0 else BAD})
	if kind == "supply":
		var heal := float(def.get("heal_amount", 0.0))
		if heal > 0.0:
			lines.append({"text": "Restores %d Health" % roundi(heal), "color": GOOD})
	var sell_v := int(def.get("sell_value", 0))
	if sell_v > 0:
		lines.append({"text": "Sell Value: %d Credits" % sell_v, "color": Bf2Screen.ACCENT})
	var desc := str(def.get("description", ""))
	if desc != "":
		lines.append({"text": desc, "color": Bf2Screen.TEXT})

	var stats: Dictionary = store.resolve_item_stats(def_id, quality, true) if store != null else {}
	var eq_stats: Dictionary = {}
	if not equipped_def.is_empty() and store != null:
		var eq_q := float(equipped_inst.get("quality", 0.5)) if not equipped_inst.is_empty() else 0.5
		eq_stats = store.resolve_item_stats(str(equipped_def.get("id", "")), eq_q, true)

	if kind == "weapon" and not stats.is_empty():
		var dmg := float(stats.get("damage", 0.0))
		var delay := maxf(0.01, float(stats.get("shot_delay", 0.2)))
		var rof := 1.0 / delay
		var dtype := str(stats.get("damage_type", "laser")).capitalize()
		if str(stats.get("projectile", "")) == "melee":
			var eq_dmg := float(eq_stats.get("damage", dmg)) if not eq_stats.is_empty() else dmg
			var eq_rof := 1.0 / maxf(0.01, float(eq_stats.get("shot_delay", delay))) if not eq_stats.is_empty() else rof
			var reach := float(stats.get("melee_range", 2.6))
			var eq_reach := float(eq_stats.get("melee_range", reach)) if not eq_stats.is_empty() else reach
			lines.append(_cmp_line("Damage (%s)" % dtype, dmg, eq_dmg, "%.0f" % dmg, true, not eq_stats.is_empty()))
			lines.append(_cmp_line("Swing Rate", rof, eq_rof, "%.1f /s" % rof, true, not eq_stats.is_empty()))
			lines.append(_cmp_line("Reach", reach, eq_reach, "%.1f m" % reach, true, not eq_stats.is_empty()))
			lines.append({"text": "Lunge: %.1f m/s   Stagger: %.2fs" % [float(stats.get("melee_lunge", 5.0)),
					float(stats.get("melee_stagger", 0.35))], "color": Bf2Screen.TEXT})
			lines.append({"text": "Shield Disrupt: %.1fs" % float(stats.get("shield_disrupt", 2.5)), "color": Bf2Screen.TEXT})
		else:
			var clip := int(stats.get("rounds_per_clip", 0))
			var heat := float(stats.get("heat_per_shot", 0.0))
			var spread := float(stats.get("yaw_spread", 0.0))
			var speed := float(stats.get("bolt_speed", 0.0))
			var range_m := speed * float(stats.get("bolt_life", 0.0))
			var salvo := int(stats.get("shots_per_salvo", 1))
			var exp_dmg := float(stats.get("explosion_damage", 0.0))

			var eq_dmg := float(eq_stats.get("damage", dmg)) if not eq_stats.is_empty() else dmg
			var eq_rof := 1.0 / maxf(0.01, float(eq_stats.get("shot_delay", delay))) if not eq_stats.is_empty() else rof
			var eq_spread := float(eq_stats.get("yaw_spread", spread)) if not eq_stats.is_empty() else spread
			var eq_range := float(eq_stats.get("bolt_speed", speed)) * float(eq_stats.get("bolt_life", float(stats.get("bolt_life", 0.0)))) if not eq_stats.is_empty() else range_m

			lines.append(_cmp_line("Damage (%s)" % dtype, dmg, eq_dmg, "%.0f" % dmg + (" x%d" % salvo if salvo > 1 else ""), true, not eq_stats.is_empty()))
			if exp_dmg > 0:
				lines.append({"text": "Blast: %.0f dmg (%.1f m)" % [exp_dmg, float(stats.get("explosion_outer", 0.0))], "color": Bf2Screen.TEXT})
			lines.append(_cmp_line("Fire Rate", rof, eq_rof, "%.1f /s" % rof, true, not eq_stats.is_empty()))
			lines.append(_cmp_line("Spread", spread, eq_spread, "%.2f deg" % spread, false, not eq_stats.is_empty()))
			lines.append(_cmp_line("Range", range_m, eq_range, "%.0f m" % range_m, true, not eq_stats.is_empty()))
			if clip > 0:
				lines.append({"text": "Clip: %d  (Reload %.1f s)" % [clip, float(stats.get("reload_time", 1.5))], "color": Bf2Screen.TEXT})
			elif heat > 0:
				lines.append({"text": "Heat / shot: %d%%" % roundi(heat * 100.0), "color": Bf2Screen.TEXT})

	var mods: Array = stats.get("modifiers", def.get("modifiers", []))
	var eq_map := {}
	for em: Dictionary in eq_stats.get("modifiers", []):
		eq_map[str(em.get("stat_name", ""))] = float(em.get("add", 0.0))
	for m: Dictionary in mods:
		var txt := format_modifier(m)
		var sname := str(m.get("stat_name", ""))
		var add := float(m.get("add", 0.0))
		var col := Bf2Screen.TEXT
		if not eq_stats.is_empty() and absf(add) > 1e-4:
			var diff := add - float(eq_map.get(sname, 0.0))
			if absf(diff) > 1e-4:
				var sign_s := "+" if diff > 0 else ""
				var dtxt := "%s%d%%" % [sign_s, roundi(diff * 100.0)] if sname.begins_with("mitigation") else "%s%.0f" % [sign_s, diff]
				txt += " (%s)" % dtxt
				col = GOOD if diff > 0 else BAD
		lines.append({"text": txt, "color": col})

	if not equipped_def.is_empty():
		lines.append({"text": "Equipped: %s" % str(equipped_def.get("name", "")), "color": Bf2Screen.DIM})
	return lines


static func _cmp_line(label: String, val: float, eq_val: float, val_str: String, higher_better: bool, has_eq: bool) -> Dictionary:
	var txt := "%s: %s" % [label, val_str]
	var col := Bf2Screen.TEXT
	if has_eq and absf(val - eq_val) > 0.05:
		var better := (val > eq_val) if higher_better else (val < eq_val)
		var diff := val - eq_val
		txt += " (%s%.1f)" % ["+" if diff > 0 else "", diff]
		col = GOOD if better else BAD
	return {"text": txt, "color": col}


static func draw(screen: Control, at: Vector2, title: String, lines: Array) -> void:
	if screen.art == null or lines.is_empty():
		return
	var u: float = screen.u
	var w := 270.0 * u
	var line_h := 16.0 * u
	# Long lines (a description) wrap to the box's width.
	var rows: Array = []  # [text, color]
	for entry: Dictionary in lines:
		for row in wrap_rows(screen.art, "gamefont_tiny", str(entry["text"]), w - 20 * u, u):
			rows.append([row, entry["color"]])
	var title_rows := wrap_rows(screen.art, "gamefont_small", title, w - 20 * u, u)
	var title_h := 20.0 * title_rows.size() + 8.0
	var h := (title_h + 6.0 + rows.size() * 16.0) * u
	var pos := at + Vector2(14, 14) * u
	if pos.x + w > screen.size.x - 8 * u:
		pos.x = at.x - w - 10 * u
	if pos.y + h > screen.size.y - 8 * u:
		pos.y = screen.size.y - h - 8 * u
	pos.x = maxf(8 * u, pos.x)
	pos.y = maxf(8 * u, pos.y)
	var box := Rect2(pos, Vector2(w, h))
	screen.frame(box, "border_popup")
	for i in title_rows.size():
		screen.text("gamefont_small", title_rows[i], Vector2(box.position.x + 10 * u, box.position.y + (8 + 20 * i) * u),
				"Left", Bf2Screen.ACCENT)
	var y := box.position.y + title_h * u
	for row: Array in rows:
		screen.text("gamefont_tiny", row[0], Vector2(box.position.x + 10 * u, y), "Left", row[1])
		y += line_h


# `text` broken at spaces into rows no wider than `width` (pixels) in a
# Battlefront font at `scale`.
static func wrap_rows(art: RefCounted, font: String, text: String, width: float, scale: float) -> PackedStringArray:
	var rows := PackedStringArray()
	var row := ""
	for word in text.split(" ", false):
		var candidate := word if row == "" else row + " " + word
		if row != "" and art.text_width(font, candidate, scale) > width:
			rows.append(row)
			row = word
		else:
			row = candidate
	if row != "" or rows.is_empty():
		rows.append(row)
	return rows
