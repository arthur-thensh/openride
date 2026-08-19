extends Node3D

# OpenRide Godot Drive prototype
# ------------------------------
# This is intentionally a frontend-only experiment. One world unit = one meter.
# The route and context roads are synthetic so the experiment can run with no
# ORMap, routing graph, Android plugin or native extension.

const SPEED_MPS := 60.0 / 3.6
const CAMERA_RESPONSE := 4.5
const HEADING_RESPONSE := 5.5
const CONTROLS_VISIBLE_SECONDS := 4.0

const COLOR_GROUND := Color(0.87, 0.89, 0.85)
const COLOR_ROAD_CASING := Color(0.43, 0.46, 0.43)
const COLOR_ROAD_FILL := Color(0.97, 0.97, 0.94)
const COLOR_MINOR_ROAD := Color(0.58, 0.61, 0.57)
const COLOR_TRAIL := Color(0.34, 0.45, 0.28)
const COLOR_ROUTE_CASING := Color(0.05, 0.10, 0.15)
const COLOR_ROUTE := Color(0.10, 0.46, 0.82)
const COLOR_OPENRIDE := Color(0.18, 0.78, 0.71)
const COLOR_PANEL := Color(0.045, 0.065, 0.075, 0.93)
const COLOR_PANEL_SOFT := Color(0.045, 0.065, 0.075, 0.84)

var route_points := PackedVector3Array([
    Vector3(-58.0, 0.0, 250.0),
    Vector3(-54.0, 0.0, 218.0),
    Vector3(-46.0, 0.0, 184.0),
    Vector3(-40.0, 0.0, 148.0),
    Vector3(-39.0, 0.0, 112.0),
    Vector3(-22.0, 0.0, 83.0),
    Vector3(8.0, 0.0, 64.0),
    Vector3(28.0, 0.0, 37.0),
    Vector3(31.0, 0.0, 4.0),
    Vector3(23.0, 0.0, -30.0),
    Vector3(47.0, 0.0, -60.0),
    Vector3(53.0, 0.0, -98.0),
    Vector3(36.0, 0.0, -128.0),
    Vector3(10.0, 0.0, -149.0),
    Vector3(3.0, 0.0, -184.0),
    Vector3(21.0, 0.0, -219.0),
    Vector3(48.0, 0.0, -248.0),
    Vector3(55.0, 0.0, -282.0),
    Vector3(42.0, 0.0, -320.0)
])

var route_cumulative := PackedFloat32Array()
var route_length := 0.0
var maneuvers: Array[Dictionary] = []
var travel_m := 0.0
var camera_forward := Vector3(0.0, 0.0, -1.0)
var heading_up := true
var overview_mode := false
var simulation_paused := false

var bike: Node3D
var camera: Camera3D
var controls_panel: Control
var controls_timer: Timer
var primary_label: Label
var instruction_label: Label
var maneuver_symbol: Label
var gps_label: Label
var speed_label: Label
var remaining_label: Label
var arrival_label: Label
var north_button: Button
var gps_button: Button


func _ready() -> void:
    _rebuild_route_metrics()
    _build_environment()
    _build_ground()
    _build_context_roads()
    _build_active_route()
    _build_bike()
    _build_camera()
    _build_hud()
    _update_world(0.0, true)
    print("OPENRIDE_GODOT prototype_ready=1 route_m=%.1f speed_kph=60" % route_length)


func _process(delta: float) -> void:
    if not simulation_paused:
        travel_m += SPEED_MPS * delta
        if travel_m >= route_length:
            travel_m = fmod(travel_m, route_length)
            _show_controls()
    _update_world(delta, false)


func _unhandled_input(event: InputEvent) -> void:
    if event is InputEventScreenTouch and event.pressed:
        _show_controls()
    elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
        _show_controls()


func _build_environment() -> void:
    var world_environment := WorldEnvironment.new()
    var environment := Environment.new()
    environment.background_mode = Environment.BG_COLOR
    environment.background_color = Color(0.83, 0.87, 0.84)
    environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
    environment.ambient_light_color = Color(1.0, 1.0, 1.0)
    environment.ambient_light_energy = 0.82
    world_environment.environment = environment
    add_child(world_environment)

    var sun := DirectionalLight3D.new()
    sun.rotation_degrees = Vector3(-58.0, -24.0, 0.0)
    sun.light_energy = 0.55
    sun.shadow_enabled = false
    add_child(sun)


func _build_ground() -> void:
    var ground := MeshInstance3D.new()
    var plane := PlaneMesh.new()
    plane.size = Vector2(520.0, 760.0)
    ground.mesh = plane
    ground.position = Vector3(0.0, -0.08, -38.0)
    ground.material_override = _material(COLOR_GROUND)
    add_child(ground)


func _build_context_roads() -> void:
    var roads: Array[PackedVector3Array] = [
        PackedVector3Array([Vector3(-180,0,205), Vector3(-90,0,190), Vector3(-46,0,184), Vector3(30,0,175), Vector3(150,0,180)]),
        PackedVector3Array([Vector3(-150,0,118), Vector3(-92,0,115), Vector3(-39,0,112), Vector3(45,0,104), Vector3(160,0,91)]),
        PackedVector3Array([Vector3(-160,0,42), Vector3(-75,0,49), Vector3(8,0,64), Vector3(94,0,60), Vector3(170,0,46)]),
        PackedVector3Array([Vector3(-170,0,-18), Vector3(-68,0,-20), Vector3(23,0,-30), Vector3(100,0,-18), Vector3(175,0,-5)]),
        PackedVector3Array([Vector3(-155,0,-102), Vector3(-72,0,-112), Vector3(36,0,-128), Vector3(112,0,-124), Vector3(172,0,-138)]),
        PackedVector3Array([Vector3(-170,0,-186), Vector3(-95,0,-181), Vector3(3,0,-184), Vector3(92,0,-179), Vector3(165,0,-190)]),
        PackedVector3Array([Vector3(-148,0,-250), Vector3(-56,0,-255), Vector3(48,0,-248), Vector3(127,0,-259), Vector3(175,0,-278)]),
        PackedVector3Array([Vector3(-110,0,285), Vector3(-100,0,185), Vector3(-92,0,95), Vector3(-84,0,10), Vector3(-73,0,-87), Vector3(-65,0,-205), Vector3(-58,0,-335)]),
        PackedVector3Array([Vector3(82,0,285), Vector3(75,0,180), Vector3(68,0,84), Vector3(64,0,-12), Vector3(58,0,-110), Vector3(61,0,-220), Vector3(72,0,-340)]),
        PackedVector3Array([Vector3(145,0,240), Vector3(110,0,135), Vector3(92,0,38), Vector3(92,0,-55), Vector3(116,0,-150), Vector3(138,0,-245)])
    ]

    for road in roads:
        add_child(_polyline_mesh(road, 5.2, COLOR_ROAD_CASING, 0.005))
        add_child(_polyline_mesh(road, 3.1, COLOR_ROAD_FILL, 0.012))

    var minor_roads: Array[PackedVector3Array] = [
        PackedVector3Array([Vector3(-128,0,260), Vector3(-76,0,155), Vector3(-20,0,88), Vector3(36,0,28)]),
        PackedVector3Array([Vector3(-142,0,60), Vector3(-80,0,16), Vector3(-20,0,-46), Vector3(45,0,-116), Vector3(108,0,-168)]),
        PackedVector3Array([Vector3(-120,0,-310), Vector3(-62,0,-255), Vector3(4,0,-206), Vector3(74,0,-154), Vector3(142,0,-120)]),
        PackedVector3Array([Vector3(8,0,285), Vector3(4,0,180), Vector3(1,0,92), Vector3(-4,0,10), Vector3(-16,0,-88), Vector3(-30,0,-210), Vector3(-26,0,-340)])
    ]
    for road in minor_roads:
        add_child(_polyline_mesh(road, 2.0, COLOR_MINOR_ROAD, 0.02))

    var trails: Array[PackedVector3Array] = [
        PackedVector3Array([Vector3(-175,0,155), Vector3(-135,0,126), Vector3(-112,0,78), Vector3(-85,0,51)]),
        PackedVector3Array([Vector3(110,0,10), Vector3(133,0,-32), Vector3(148,0,-82), Vector3(161,0,-145)]),
        PackedVector3Array([Vector3(-145,0,-145), Vector3(-118,0,-176), Vector3(-102,0,-226), Vector3(-82,0,-282)])
    ]
    for trail in trails:
        add_child(_polyline_mesh(trail, 1.35, COLOR_TRAIL, 0.024))


func _build_active_route() -> void:
    add_child(_polyline_mesh(route_points, 8.0, COLOR_ROUTE_CASING, 0.038))
    add_child(_polyline_mesh(route_points, 4.8, COLOR_ROUTE, 0.052))


func _build_bike() -> void:
    bike = Node3D.new()
    bike.name = "Rider"
    add_child(bike)

    var halo := MeshInstance3D.new()
    var halo_mesh := BoxMesh.new()
    halo_mesh.size = Vector3(1.8, 0.10, 3.9)
    halo.mesh = halo_mesh
    halo.position.y = 0.10
    halo.material_override = _material(Color(0.97, 0.98, 0.98))
    bike.add_child(halo)

    var body := MeshInstance3D.new()
    var body_mesh := BoxMesh.new()
    body_mesh.size = Vector3(1.15, 0.45, 2.8)
    body.mesh = body_mesh
    body.position.y = 0.35
    body.material_override = _material(COLOR_ROUTE)
    bike.add_child(body)

    var nose := MeshInstance3D.new()
    var prism := PrismMesh.new()
    prism.size = Vector3(1.5, 0.42, 1.7)
    nose.mesh = prism
    nose.position = Vector3(0.0, 0.34, -1.75)
    nose.rotation_degrees = Vector3(0.0, 90.0, 0.0)
    nose.material_override = _material(COLOR_ROUTE)
    bike.add_child(nose)

    for z in [-1.15, 1.15]:
        var wheel := MeshInstance3D.new()
        var cylinder := CylinderMesh.new()
        cylinder.top_radius = 0.42
        cylinder.bottom_radius = 0.42
        cylinder.height = 0.34
        cylinder.radial_segments = 20
        wheel.mesh = cylinder
        wheel.position = Vector3(0.0, 0.32, z)
        wheel.rotation_degrees.z = 90.0
        wheel.material_override = _material(Color(0.04, 0.06, 0.07))
        bike.add_child(wheel)


func _build_camera() -> void:
    camera = Camera3D.new()
    camera.name = "DriveCamera"
    camera.current = true
    camera.fov = 52.0
    camera.near = 0.25
    camera.far = 1400.0
    add_child(camera)


func _build_hud() -> void:
    var canvas := CanvasLayer.new()
    canvas.layer = 10
    add_child(canvas)

    var top_panel := ColorRect.new()
    top_panel.color = COLOR_PANEL
    top_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
    top_panel.offset_left = 14.0
    top_panel.offset_top = 14.0
    top_panel.offset_right = -14.0
    top_panel.offset_bottom = 116.0
    top_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    canvas.add_child(top_panel)

    maneuver_symbol = Label.new()
    maneuver_symbol.text = "↱"
    maneuver_symbol.position = Vector2(12, 10)
    maneuver_symbol.size = Vector2(68, 78)
    maneuver_symbol.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    maneuver_symbol.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    maneuver_symbol.add_theme_font_size_override("font_size", 48)
    maneuver_symbol.add_theme_color_override("font_color", COLOR_OPENRIDE)
    top_panel.add_child(maneuver_symbol)

    primary_label = Label.new()
    primary_label.position = Vector2(86, 9)
    primary_label.size = Vector2(260, 38)
    primary_label.add_theme_font_size_override("font_size", 28)
    primary_label.add_theme_color_override("font_color", COLOR_OPENRIDE)
    top_panel.add_child(primary_label)

    instruction_label = Label.new()
    instruction_label.position = Vector2(87, 49)
    instruction_label.size = Vector2(300, 36)
    instruction_label.add_theme_font_size_override("font_size", 17)
    instruction_label.add_theme_color_override("font_color", Color(0.96, 0.97, 0.98))
    top_panel.add_child(instruction_label)

    gps_label = Label.new()
    gps_label.position = Vector2(286, 12)
    gps_label.size = Vector2(106, 24)
    gps_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
    gps_label.add_theme_font_size_override("font_size", 12)
    gps_label.add_theme_color_override("font_color", Color(0.42, 0.88, 0.55))
    gps_label.text = "GPS • 4 m"
    top_panel.add_child(gps_label)

    var prototype_label := Label.new()
    prototype_label.text = "GODOT FRONTEND PROTOTYPE"
    prototype_label.position = Vector2(87, 79)
    prototype_label.size = Vector2(240, 18)
    prototype_label.add_theme_font_size_override("font_size", 10)
    prototype_label.add_theme_color_override("font_color", Color(0.64, 0.69, 0.70))
    top_panel.add_child(prototype_label)

    var stats_panel := ColorRect.new()
    stats_panel.color = COLOR_PANEL_SOFT
    stats_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
    stats_panel.offset_left = 14.0
    stats_panel.offset_top = -58.0
    stats_panel.offset_right = -14.0
    stats_panel.offset_bottom = -14.0
    stats_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    canvas.add_child(stats_panel)

    speed_label = _stat_label(stats_panel, 0.0, 0.333)
    remaining_label = _stat_label(stats_panel, 0.333, 0.666)
    arrival_label = _stat_label(stats_panel, 0.666, 1.0)

    controls_panel = ColorRect.new()
    controls_panel.color = COLOR_PANEL
    controls_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
    controls_panel.offset_left = 14.0
    controls_panel.offset_top = -118.0
    controls_panel.offset_right = -14.0
    controls_panel.offset_bottom = -66.0
    canvas.add_child(controls_panel)

    var controls := HBoxContainer.new()
    controls.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    controls.add_theme_constant_override("separation", 2)
    controls_panel.add_child(controls)

    _control_button(controls, "CARTE", _on_map_pressed)
    _control_button(controls, "CENTRER", _on_recenter_pressed)
    north_button = _control_button(controls, "NORD", _on_north_pressed)
    gps_button = _control_button(controls, "GPS", _on_gps_pressed)

    controls_timer = Timer.new()
    controls_timer.one_shot = true
    controls_timer.wait_time = CONTROLS_VISIBLE_SECONDS
    controls_timer.timeout.connect(_hide_controls)
    add_child(controls_timer)
    _show_controls()


func _stat_label(parent: Control, left: float, right: float) -> Label:
    var label := Label.new()
    label.anchor_left = left
    label.anchor_right = right
    label.anchor_top = 0.0
    label.anchor_bottom = 1.0
    label.offset_left = 4.0
    label.offset_right = -4.0
    label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 16)
    label.add_theme_color_override("font_color", Color(0.97, 0.98, 0.98))
    parent.add_child(label)
    return label


func _control_button(parent: Control, text: String, callback: Callable) -> Button:
    var button := Button.new()
    button.text = text
    button.flat = true
    button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    button.add_theme_font_size_override("font_size", 13)
    button.add_theme_color_override("font_color", Color(0.82, 0.85, 0.86))
    button.pressed.connect(callback)
    parent.add_child(button)
    return button


func _show_controls() -> void:
    if controls_panel == null:
        return
    controls_panel.visible = true
    if controls_timer != null:
        controls_timer.start(CONTROLS_VISIBLE_SECONDS)
    print("OPENRIDE_GODOT_HUD controls_visible=1")


func _hide_controls() -> void:
    if controls_panel != null:
        controls_panel.visible = false
        print("OPENRIDE_GODOT_HUD controls_visible=0")


func _on_map_pressed() -> void:
    overview_mode = not overview_mode
    _show_controls()


func _on_recenter_pressed() -> void:
    overview_mode = false
    heading_up = true
    north_button.text = "NORD"
    _show_controls()


func _on_north_pressed() -> void:
    heading_up = not heading_up
    north_button.text = "CAP" if not heading_up else "NORD"
    _show_controls()


func _on_gps_pressed() -> void:
    simulation_paused = not simulation_paused
    gps_button.text = "GPS ▶" if simulation_paused else "GPS"
    gps_label.text = "GPS PAUSE" if simulation_paused else "GPS • 4 m"
    _show_controls()


func _update_world(delta: float, immediate: bool) -> void:
    var rider_position := _sample_position(travel_m)
    var heading := _smoothed_route_heading(travel_m)
    if heading.length_squared() < 0.001:
        heading = Vector3(0.0, 0.0, -1.0)

    if immediate:
        camera_forward = heading
    else:
        var heading_alpha := 1.0 - exp(-delta * HEADING_RESPONSE)
        camera_forward = camera_forward.lerp(heading, heading_alpha).normalized()

    bike.global_position = rider_position + Vector3.UP * 0.18
    bike.rotation.y = atan2(-heading.x, -heading.z)

    var view_forward := camera_forward if heading_up else Vector3(0.0, 0.0, -1.0)
    var camera_back := 18.0
    var camera_height := 22.0
    var lookahead := 34.0
    var fov := 52.0
    if overview_mode:
        camera_back = 30.0
        camera_height = 34.0
        lookahead = 48.0
        fov = 58.0

    var desired_camera := rider_position - view_forward * camera_back + Vector3.UP * camera_height
    if immediate:
        camera.global_position = desired_camera
    else:
        var camera_alpha := 1.0 - exp(-delta * CAMERA_RESPONSE)
        camera.global_position = camera.global_position.lerp(desired_camera, camera_alpha)
    camera.fov = lerpf(camera.fov, fov, 1.0 if immediate else (1.0 - exp(-delta * 3.0)))
    camera.look_at(rider_position + view_forward * lookahead + Vector3.UP * 0.8, Vector3.UP)

    _update_hud()


func _update_hud() -> void:
    if primary_label == null:
        return

    var next := _next_maneuver(travel_m)
    if next.is_empty():
        primary_label.text = "CONTINUER"
        instruction_label.text = "Suivre l'itinéraire"
        maneuver_symbol.text = "↑"
    else:
        var distance_m: float = max(0.0, float(next["distance"]) - travel_m)
        if distance_m < 1000.0:
            primary_label.text = "DANS %.0f m" % distance_m
        else:
            primary_label.text = "DANS %.1f km" % (distance_m / 1000.0)
        instruction_label.text = String(next["text"])
        maneuver_symbol.text = String(next["symbol"])

    var remaining: float = max(0.0, route_length - travel_m)
    speed_label.text = "%.0f km/h" % (SPEED_MPS * 3.6)
    remaining_label.text = "%.1f km" % (remaining / 1000.0)
    arrival_label.text = _arrival_clock(remaining / SPEED_MPS)


func _arrival_clock(seconds_from_now: float) -> String:
    var now := Time.get_time_dict_from_system()
    var current_minutes := int(now["hour"]) * 60 + int(now["minute"])
    var arrival_minutes := current_minutes + int(ceil(seconds_from_now / 60.0))
    arrival_minutes %= 24 * 60
    return "%02d:%02d" % [arrival_minutes / 60, arrival_minutes % 60]


func _rebuild_route_metrics() -> void:
    route_cumulative = PackedFloat32Array()
    route_cumulative.resize(route_points.size())
    route_cumulative[0] = 0.0
    route_length = 0.0
    for i in range(1, route_points.size()):
        route_length += route_points[i - 1].distance_to(route_points[i])
        route_cumulative[i] = route_length

    maneuvers.clear()
    for i in range(1, route_points.size() - 1):
        var before := (route_points[i] - route_points[i - 1]).normalized()
        var after := (route_points[i + 1] - route_points[i]).normalized()
        var angle := acos(clampf(before.dot(after), -1.0, 1.0))
        if angle < deg_to_rad(18.0):
            continue
        var cross_y := before.cross(after).y
        var right_turn := cross_y > 0.0
        maneuvers.append({
            "distance": float(route_cumulative[i]),
            "text": "Tourner à droite" if right_turn else "Tourner à gauche",
            "symbol": "↱" if right_turn else "↰"
        })


func _next_maneuver(distance_m: float) -> Dictionary:
    for maneuver in maneuvers:
        if float(maneuver["distance"]) > distance_m + 3.0:
            return maneuver
    return {}


func _sample_position(distance_m: float) -> Vector3:
    if route_points.size() < 2:
        return Vector3.ZERO
    var d := clampf(distance_m, 0.0, route_length)
    for i in range(route_points.size() - 1):
        var start_d := float(route_cumulative[i])
        var end_d := float(route_cumulative[i + 1])
        if d <= end_d or i == route_points.size() - 2:
            var span: float = max(0.001, end_d - start_d)
            var t := clampf((d - start_d) / span, 0.0, 1.0)
            return route_points[i].lerp(route_points[i + 1], t)
    return route_points[route_points.size() - 1]


func _smoothed_route_heading(distance_m: float) -> Vector3:
    var behind := _sample_position(max(0.0, distance_m - 7.0))
    var ahead := _sample_position(min(route_length, distance_m + 11.0))
    var direction := ahead - behind
    direction.y = 0.0
    return direction.normalized()


func _material(color: Color) -> StandardMaterial3D:
    var material := StandardMaterial3D.new()
    material.albedo_color = color
    material.roughness = 1.0
    return material


func _polyline_mesh(points: PackedVector3Array, width: float, color: Color, y_offset: float) -> MeshInstance3D:
    var instance := MeshInstance3D.new()
    if points.size() < 2:
        return instance

    var left := PackedVector3Array()
    var right := PackedVector3Array()
    left.resize(points.size())
    right.resize(points.size())

    for i in range(points.size()):
        var direction: Vector3
        if i == 0:
            direction = points[1] - points[0]
        elif i == points.size() - 1:
            direction = points[i] - points[i - 1]
        else:
            direction = points[i + 1] - points[i - 1]
        direction.y = 0.0
        direction = direction.normalized()
        var side := Vector3(-direction.z, 0.0, direction.x)
        var elevated := points[i] + Vector3.UP * y_offset
        left[i] = elevated + side * width * 0.5
        right[i] = elevated - side * width * 0.5

    var surface := SurfaceTool.new()
    surface.begin(Mesh.PRIMITIVE_TRIANGLES)
    for i in range(points.size() - 1):
        _surface_vertex(surface, left[i])
        _surface_vertex(surface, right[i])
        _surface_vertex(surface, left[i + 1])
        _surface_vertex(surface, right[i])
        _surface_vertex(surface, right[i + 1])
        _surface_vertex(surface, left[i + 1])

    instance.mesh = surface.commit()
    instance.material_override = _material(color)
    return instance


func _surface_vertex(surface: SurfaceTool, vertex: Vector3) -> void:
    surface.set_normal(Vector3.UP)
    surface.add_vertex(vertex)
