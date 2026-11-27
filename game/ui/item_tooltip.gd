# Shared Battlefront II-styled item tooltip renderer for Inventory & Quartermaster:
# shows item name (in its grade's color), kind/slot, faction, grade, every
# stat, and color-coded deltas against the currently equipped item in the
# matching slot.
extends RefCounted

const Bf2Screen = preload("res://ui/bf2_screen.gd")
const ItemGrade = preload("res://ui/item_grade.gd")
const GOOD := Color(0.45, 0.95, 0.5)
const BAD := Color(1.0, 0.4, 0.35)
const MARK_NUMERALS := ["", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"]


# An item's name with its owned copy's Mk tier ("E-11 Blaster Rifle Mk IV").
static func item_name(def: Dictionary, inst: Dictionary = {}) -> String:
	var base := str(def.get("name", def.get("id", inst.get("def_id", "?"))))
	var mark := clampi(int(inst.get("mark", 0)), 0, 10)
	return base if mark == 0 else "%s Mk %s" % [base, MARK_NUMERALS[mark]]


# The color its name shows in: its grade's (white for an ungraded item).
static func name_color(def: Dictionary, inst: Dictionary = {}) -> Color:
	return ItemGrade.color(def, inst)


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
		"healing_received": return "Healing Received"
		"ability_cooldown": return "Ability Cooldowns"
		"effect_resistance": return "Debuff Resist"
		"xp_gain": return "Experience Gained"
		"faction_gain": return "Faction Points Gained"
	return stat_name.replace("_", " ").capitalize()


static func format_modifier(m: Dictionary) -> String:
	var raw_name := str(m.get("stat_name", ""))
	var sname := stat_label(raw_name)
	var add := float(m.get("add", 0.0))
	var mult := float(m.get("multiply", 1.0))
	var parts: PackedStringArray = []
	if absf(add) > 1e-4:
		if raw_name.begins_with("mitigation") or raw_name in ["explosion_push_resist", "effect_resistance"]:
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
	var grade := int(inst.get("grade", 1)) if not inst.is_empty() else 1
	var kind := str(def.get("kind", "weapon"))
	var slot := str(def.get("slot", ""))
	var faction := str(def.get("faction", "neutral")).capitalize()
	lines.append({"text": "%s | %s | %s" % [kind.replace("_", " ").capitalize(), slot.replace("_", " ").capitalize(), faction],
			"color": Bf2Screen.DIM})
	var grade_label := ItemGrade.label(def, inst)
	if grade_label != "":
		lines.append({"text": grade_label, "color": ItemGrade.color(def, inst)})
	if kind == "bodysuit":
		lines.append({"text": "A look only: worn in the Cosmetic Override, in place of everything",
				"color": Bf2Screen.ACCENT})
	var only: PackedStringArray = def.get("species", PackedStringArray())
	if not only.is_empty():
		var names: PackedStringArray = []
		for s: String in only:
			names.append(s.capitalize())
		lines.append({"text": "%s only" % " / ".join(names), "color": Bf2Screen.ACCENT})
	var mark := clampi(int(inst.get("mark", 0)), 0, 10)
	if mark > 0:
		var req: Dictionary = store.mark_requirement(def_id, mark) if store != null else {}
		var prof := str(req.get("profession", ""))
		lines.append({"text": "Mk %s: requires %slevel %d" % [MARK_NUMERALS[mark], prof + " " if prof != "" else "",
				int(req.get("level", mark * 10))], "color": Bf2Screen.ACCENT})
		var health := float(req.get("health", 0.0))
		if health > 0.0:
			lines.append({"text": "Max Health: +%d%% (with a full kit)" % roundi(health * 100.0), "color": GOOD})
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
		if def.has("hit_effect"):
			lines.append(effect_line(def["hit_effect"]))
	if kind == "supply":
		var heal := float(def.get("heal_amount", 0.0))
		if heal > 0.0:
			lines.append({"text": "Restores %d Health (out of combat)" % roundi(heal), "color": GOOD})
	var sell_v := int(def.get("sell_value", 0))
	if sell_v > 0:
		lines.append({"text": "Sell Value: %d Credits" % sell_v, "color": Bf2Screen.ACCENT})
	var desc := str(def.get("description", ""))
	if desc != "":
		lines.append({"text": desc, "color": Bf2Screen.TEXT})

	# As the modules crafted into it tune it (docs/CRAFTING.md).
	var stats: Dictionary = store.resolve_item_stats(def_id, grade, mark,
			str(inst.get("color_json", "{}"))) if store != null else {}
	var eq_stats: Dictionary = {}
	if not equipped_def.is_empty() and store != null:
		var eq_g := int(equipped_inst.get("grade", 1)) if not equipped_inst.is_empty() else 1
		eq_stats = store.resolve_item_stats(str(equipped_def.get("id", "")), eq_g,
				int(equipped_inst.get("mark", 0)), str(equipped_inst.get("color_json", "{}")))
	var modules: PackedStringArray = stats.get("modules", PackedStringArray())
	if not modules.is_empty():
		lines.append({"text": "Modules: " + ", ".join(modules), "color": Bf2Screen.ACCENT})

	if kind == "weapon" and not stats.is_empty():
		var dmg := float(stats.get("damage", 0.0))
		var delay := maxf(0.01, float(stats.get("shot_delay", 0.2)))
		var rof := 1.0 / delay
		var dtype := str(stats.get("damage_type", "laser")).capitalize()
		if str(stats.get("projectile", "")) == "heal":
			# A medpack: what it heals (at once or over time), how many a life.
			var heal := float(stats.get("heal", 0.0))
			var eq_heal := float(eq_stats.get("heal", heal)) if not eq_stats.is_empty() else heal
			var over := float(stats.get("heal_time", 0.0))
			var heal_text := "%.0f" % heal + (" over %.0f s" % over if over > 0.0 else " at once")
			lines.append(_cmp_line("Heals", heal, eq_heal, heal_text, true, not eq_stats.is_empty()))
			lines.append({"text": "Carried: %d a life" % int(stats.get("carried", 2)), "color": Bf2Screen.TEXT})
		elif str(stats.get("projectile", "")) == "melee":
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
		lines.append_array(_handling_lines(stats))

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
		lines.append({"text": "Equipped: %s" % item_name(equipped_def, equipped_inst), "color": Bf2Screen.DIM})
	return lines


# "On hit: Burning (12 laser dmg/s, -20% Run Speed) for 3s, 35% chance".
static func effect_line(e: Dictionary) -> Dictionary:
	var parts: PackedStringArray = []
	var dps := float(e.get("damage_per_second", 0.0))
	if dps > 0.0:
		parts.append("%.0f %s dmg/s" % [dps, str(e.get("damage_type", ""))])
	for m: Dictionary in e.get("modifiers", []):
		parts.append(format_modifier(m))
	var txt := "On hit: %s" % str(e.get("name", ""))
	if not parts.is_empty():
		txt += " (%s)" % ", ".join(parts)
	txt += " for %.1fs" % float(e.get("duration", 0.0))
	var chance := float(e.get("chance", 1.0))
	if chance < 0.999:
		txt += ", %d%% chance" % roundi(chance * 100.0)
	return {"text": txt, "color": Bf2Screen.ACCENT}


# A weapon's handling beyond damage, where it isn't standard.
static func _handling_lines(stats: Dictionary) -> Array:
	var out: Array = []
	var head := float(stats.get("headshot_multiplier", 2.0))
	if absf(head - 2.0) > 1e-3:
		out.append({"text": "Headshots: x%.2f" % head, "color": GOOD if head > 2.0 else BAD})
	var crit := float(stats.get("crit_chance", 0.0))
	if crit > 0.0:
		out.append({"text": "Critical hits: %d%% for x%.2f" % [roundi(crit * 100.0), float(stats.get("crit_multiplier", 1.5))],
				"color": GOOD})
	for entry: Array in [["vehicle_factor", "Vs Vehicles"], ["shield_factor", "Vs Shields"]]:
		var f := float(stats.get(entry[0], 1.0))
		if absf(f - 1.0) > 1e-3:
			out.append({"text": "%s: %+d%%" % [entry[1], roundi((f - 1.0) * 100.0)], "color": GOOD if f > 1.0 else BAD})
	var swap := float(stats.get("swap_time", 0.4))
	if absf(swap - 0.4) > 1e-3:
		out.append({"text": "Swap: %.2fs" % swap, "color": GOOD if swap < 0.4 else BAD})
	var aim := float(stats.get("aim_speed", 1.0))
	if absf(aim - 1.0) > 1e-3:
		out.append({"text": "Aim Speed: %+d%%" % roundi((aim - 1.0) * 100.0), "color": GOOD if aim > 1.0 else BAD})
	var recoil := float(stats.get("recoil", 0.0))
	if recoil > 0.0:
		out.append({"text": "Recoil: %.1f deg" % recoil, "color": Bf2Screen.TEXT})
	if stats.has("hit_effect"):
		out.append(effect_line(stats["hit_effect"]))
	return out


static func _cmp_line(label: String, val: float, eq_val: float, val_str: String, higher_better: bool, has_eq: bool) -> Dictionary:
	var txt := "%s: %s" % [label, val_str]
	var col := Bf2Screen.TEXT
	if has_eq and absf(val - eq_val) > 0.05:
		var better := (val > eq_val) if higher_better else (val < eq_val)
		var diff := val - eq_val
		txt += " (%s%.1f)" % ["+" if diff > 0 else "", diff]
		col = GOOD if better else BAD
	return {"text": txt, "color": col}


static func draw(screen: Control, at: Vector2, title: String, lines: Array,
		title_color: Color = Bf2Screen.ACCENT) -> void:
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
				"Left", title_color)
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
