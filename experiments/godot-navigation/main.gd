extends Node3D

# OpenRide Godot Drive prototype V2
# ---------------------------------
# Frontend-only experiment. One world unit = one meter.
# The goal is to compare Godot's native 3D navigation camera with the SDL Drive
# renderer without depending on ORMap/routing yet.

const SPEED_KPH: float = 60.0
const SPEED_MPS: float = SPEED_KPH / 3.6
const CAMERA_RESPONSE: float = 4.2
const HEADING_RESPONSE: float = 5.2
const ZOOM_RESPONSE: float = 2.8
const CONTROLS_VISIBLE_SECONDS: float = 4.0
const RIDER_ANCHOR_Y: float = 0.68
const BASE_CAMERA_BACK_M: float = 28.0
const BASE_CAMERA_HEIGHT_M: float = 20.0
const BASE_LOOKAHEAD_M: float = 43.0
const BASE_FOV_DEG: float = 50.0

const COLOR_GROUND: Color = Color(0.89, 0.91, 0.87)
const COLOR_ROAD_CASING: Color = Color(0.48, 0.50, 0.47)
const COLOR_ROAD_FILL: Color = Color(0.985, 0.985, 0.965)
const COLOR_MINOR_ROAD: Color = Color(0.66, 0.68, 0.64)
const COLOR_TRAIL: Color = Color(0.38, 0.49, 0.31)
const COLOR_ROUTE_CASING: Color = Color(0.055, 0.11, 0.16)
const COLOR_ROUTE: Color = Color(0.08, 0.43, 0.82)
const COLOR_OPENRIDE: Color = Color(0.18, 0.78, 0.71)
const COLOR_PANEL: Color = Color(0.045, 0.065, 0.075, 0.93)
const COLOR_PANEL_SOFT: Color = Color(0.045, 0.065, 0.075, 0.84)

# Control points are intentionally sparse. V2 smooths them before rendering so
# the blue navigation line behaves like a road centerline rather than a chain
# of hard polygon corners.
var route_control_points: PackedVector3Array = PackedVector3Array([
    Vector3(0.0, 0.0, 650.0),
    Vector3(-7.0, 0.0, 590.0),
    Vector3(-14.0, 0.0, 525.0),
    Vector3(-18.0, 0.0, 455.0),
    Vector3(-6.0, 0.0, 390.0),
    Vector3(32.0, 0.0, 335.0),
    Vector3(66.0, 0.0, 275.0),
    Vector3(72.0, 0.0, 215.0),
    Vector3(56.0, 0.0, 158.0),
    Vector3(20.0, 0.0, 108.0),
    Vector3(-14.0, 0.0, 55.0),
    Vector3(-25.0, 0.0, -4.0),
    Vector3(-16.0, 0.0, -72.0),
    Vector3(14.0, 0.0, -132.0),
    Vector3(59.0, 0.0, -183.0),
    Vector3(96.0, 0.0, -247.0),
    Vector3(102.0, 0.0, -318.0),
    Vector3(84.0, 0.0, -389.0),
    Vector3(49.0, 0.0, -447.0),
    Vector3(11.0, 0.0, -494.0),
    Vector3(-18.0, 0.0, -552.0),
    Vector3(-12.0, 0.0, -622.0),
    Vector3(20.0, 0.0, -682.0),
    Vector3(63.0, 0.0, -734.0),
    Vector3(94.0, 0.0, -792.0),
    Vector3(111.0, 0.0, -862.0),
    Vector3(105.0, 0.0, -931.0),
    Vector3(75.0, 0.0, -992.0),
    Vector3(31.0, 0.0, -1047.0),
    Vector3(-5.0, 0.0, -1105.0),
    Vector3(-18.0, 0.0, -1168.0),
    Vector3(0.0, 0.0, -1235.0)
])

var route_points: PackedVector3Array = PackedVector3Array()
var route_cumulative: PackedFloat32Array = PackedFloat32Array()
var route_length: float = 0.0
var maneuvers: Array[Dictionary] = []
var travel_m: float = 0.0
var camera_forward: Vector3 = Vector3(0.0, 0.0, -1.0)
var camera_scale: float = 1.0
var heading_up: bool = true
var overview_mode: bool = false
var simulation_paused: bool = false
var telemetry_elapsed_s: float = 0.0

var camera: Camera3D
var rider_world: Node3D
var rider_marker: Node2D
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
    route_points = _chaikin_smooth(route_control_points, 3)
    _rebuild_route_metrics()
    _build_environment()
    _build_ground()
    _build_context_roads()
    _build_active_route()
    _build_rider_reference()
    _build_camera()
    _build_hud()
    _update_world(0.0, true)
    print("OPENRIDE_GODOT_V2 prototype_ready=1 route_m=%.1f speed_kph=%.0f" % [route_length, SPEED_KPH])


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
    var world_environment: WorldEnvironment = WorldEnvironment.new()
    var environment: Environment = Environment.new()
    environment.background_mode = Environment.BG_COLOR
    environment.background_color = Color(0.83, 0.87, 0.84)
    environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
    environment.ambient_light_color = Color(1.0, 1.0, 1.0)
    environment.ambient_light_energy = 0.86
    world_environment.environment = environment
    add_child(world_environment)

    var sun: DirectionalLight3D = DirectionalLight3D.new()
    sun.rotation_degrees = Vector3(-58.0, -24.0, 0.0)
    sun.light_energy = 0.48
    sun.shadow_enabled = false
    add_child(sun)


func _build_ground() -> void:
    var ground: MeshInstance3D = MeshInstance3D.new()
    var plane: PlaneMesh = PlaneMesh.new()
    plane.size = Vector2(1200.0, 2500.0)
    ground.mesh = plane
    ground.position = Vector3(0.0, -0.10, -300.0)
    ground.material_override = _material(COLOR_GROUND)
    add_child(ground)


func _build_context_roads() -> void:
    # A procedural road context is enough for camera evaluation and makes the
    # scene long enough for a two-minute 60 km/h replay without looping early.
    for z_i in range(-1200, 721, 120):
        var z: float = float(z_i)
        var road: PackedVector3Array = PackedVector3Array([
            Vector3(-360.0, 0.0, z - 8.0),
            Vector3(-180.0, 0.0, z + 4.0),
            Vector3(0.0, 0.0, z),
            Vector3(180.0, 0.0, z - 5.0),
            Vector3(360.0, 0.0, z + 7.0)
        ])
        road = _chaikin_smooth(road, 1)
        add_child(_polyline_mesh(road, 5.2, COLOR_ROAD_CASING, 0.004))
        add_child(_polyline_mesh(road, 3.25, COLOR_ROAD_FILL, 0.010))

    for x_i in range(-240, 241, 120):
        var x: float = float(x_i)
        var vertical: PackedVector3Array = PackedVector3Array([
            Vector3(x - 4.0, 0.0, 720.0),
            Vector3(x + 7.0, 0.0, 300.0),
            Vector3(x - 5.0, 0.0, -120.0),
            Vector3(x + 6.0, 0.0, -560.0),
            Vector3(x - 3.0, 0.0, -1260.0)
        ])
        vertical = _chaikin_smooth(vertical, 1)
        add_child(_polyline_mesh(vertical, 4.8, COLOR_ROAD_CASING, 0.004))
        add_child(_polyline_mesh(vertical, 2.9, COLOR_ROAD_FILL, 0.010))

    var minor_a: PackedVector3Array = PackedVector3Array([
        Vector3(-320.0, 0.0, 610.0),
        Vector3(-210.0, 0.0, 410.0),
        Vector3(-90.0, 0.0, 210.0),
        Vector3(45.0, 0.0, 15.0),
        Vector3(190.0, 0.0, -180.0),
        Vector3(330.0, 0.0, -380.0)
    ])
    var minor_b: PackedVector3Array = PackedVector3Array([
        Vector3(330.0, 0.0, 420.0),
        Vector3(210.0, 0.0, 210.0),
        Vector3(80.0, 0.0, 5.0),
        Vector3(-65.0, 0.0, -205.0),
        Vector3(-210.0, 0.0, -450.0),
        Vector3(-330.0, 0.0, -690.0)
    ])
    add_child(_polyline_mesh(_chaikin_smooth(minor_a, 2), 1.9, COLOR_MINOR_ROAD, 0.018))
    add_child(_polyline_mesh(_chaikin_smooth(minor_b, 2), 1.9, COLOR_MINOR_ROAD, 0.018))

    var trail_a: PackedVector3Array = PackedVector3Array([
        Vector3(-330.0, 0.0, 120.0),
        Vector3(-250.0, 0.0, -40.0),
        Vector3(-215.0, 0.0, -230.0),
        Vector3(-160.0, 0.0, -420.0)
    ])
    var trail_b: PackedVector3Array = PackedVector3Array([
        Vector3(265.0, 0.0, -520.0),
        Vector3(315.0, 0.0, -720.0),
        Vector3(280.0, 0.0, -940.0),
        Vector3(330.0, 0.0, -1170.0)
    ])
    add_child(_polyline_mesh(_chaikin_smooth(trail_a, 2), 1.15, COLOR_TRAIL, 0.022))
    add_child(_polyline_mesh(_chaikin_smooth(trail_b, 2), 1.15, COLOR_TRAIL, 0.022))


func _build_active_route() -> void:
    # Much narrower than V1: the route now reads as an overlay on the road,
    # instead of becoming a several-lane blue ribbon in the foreground.
    add_child(_polyline_mesh(route_points, 3.4, COLOR_ROUTE_CASING, 0.036))
    add_child(_polyline_mesh(route_points, 2.15, COLOR_ROUTE, 0.052))


func _build_rider_reference() -> void:
    # The geographic rider stays in 3D. The visible motorcycle marker is drawn
    # in the HUD at the exact screen projection of this point, like modern GPS
    # apps, so its apparent size never explodes under perspective.
    rider_world = Node3D.new()
    rider_world.name = "RiderWorldPosition"
    add_child(rider_world)


func _build_camera() -> void:
    camera = Camera3D.new()
    camera.name = "DriveCamera"
    camera.current = true
    camera.keep_aspect = Camera3D.KEEP_WIDTH
    camera.fov = BASE_FOV_DEG
    camera.near = 0.25
    camera.far = 3000.0
    add_child(camera)


func _build_hud() -> void:
    var canvas: CanvasLayer = CanvasLayer.new()
    canvas.layer = 10
    add_child(canvas)

    var top_panel: ColorRect = ColorRect.new()
    top_panel.color = COLOR_PANEL
    top_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
    top_panel.offset_left = 14.0
    top_panel.offset_top = 14.0
    top_panel.offset_right = -14.0
    top_panel.offset_bottom = 108.0
    top_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    canvas.add_child(top_panel)

    maneuver_symbol = Label.new()
    maneuver_symbol.text = "↱"
    maneuver_symbol.position = Vector2(10.0, 8.0)
    maneuver_symbol.size = Vector2(68.0, 72.0)
    maneuver_symbol.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    maneuver_symbol.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    maneuver_symbol.add_theme_font_size_override("font_size", 46)
    maneuver_symbol.add_theme_color_override("font_color", COLOR_OPENRIDE)
    top_panel.add_child(maneuver_symbol)

    primary_label = Label.new()
    primary_label.position = Vector2(82.0, 8.0)
    primary_label.size = Vector2(250.0, 36.0)
    primary_label.add_theme_font_size_override("font_size", 27)
    primary_label.add_theme_color_override("font_color", COLOR_OPENRIDE)
    top_panel.add_child(primary_label)

    instruction_label = Label.new()
    instruction_label.position = Vector2(83.0, 46.0)
    instruction_label.size = Vector2(300.0, 31.0)
    instruction_label.add_theme_font_size_override("font_size", 16)
    instruction_label.add_theme_color_override("font_color", Color(0.96, 0.97, 0.98))
    top_panel.add_child(instruction_label)

    gps_label = Label.new()
    gps_label.position = Vector2(292.0, 12.0)
    gps_label.size = Vector2(102.0, 22.0)
    gps_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
    gps_label.add_theme_font_size_override("font_size", 11)
    gps_label.add_theme_color_override("font_color", Color(0.42, 0.88, 0.55))
    gps_label.text = "GPS • 4 m"
    top_panel.add_child(gps_label)

    var prototype_label: Label = Label.new()
    prototype_label.text = "GODOT V2 · NATIVE 3D CAMERA"
    prototype_label.position = Vector2(83.0, 76.0)
    prototype_label.size = Vector2(240.0, 15.0)
    prototype_label.add_theme_font_size_override("font_size", 9)
    prototype_label.add_theme_color_override("font_color", Color(0.62, 0.67, 0.68))
    top_panel.add_child(prototype_label)

    var stats_panel: ColorRect = ColorRect.new()
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
    controls_panel.offset_top = -114.0
    controls_panel.offset_right = -14.0
    controls_panel.offset_bottom = -64.0
    canvas.add_child(controls_panel)

    var controls: HBoxContainer = HBoxContainer.new()
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

    rider_marker = Node2D.new()
    rider_marker.name = "RiderMarker"
    canvas.add_child(rider_marker)
    _build_rider_marker_geometry()

    _show_controls()


func _build_rider_marker_geometry() -> void:
    var shadow: Polygon2D = Polygon2D.new()
    shadow.polygon = PackedVector2Array([
        Vector2(-11.0, 12.0), Vector2(0.0, -18.0), Vector2(11.0, 12.0),
        Vector2(5.0, 9.0), Vector2(0.0, 18.0), Vector2(-5.0, 9.0)
    ])
    shadow.color = Color(0.02, 0.04, 0.05, 0.38)
    shadow.position = Vector2(1.5, 3.0)
    rider_marker.add_child(shadow)

    var casing: Polygon2D = Polygon2D.new()
    casing.polygon = PackedVector2Array([
        Vector2(-11.0, 12.0), Vector2(0.0, -18.0), Vector2(11.0, 12.0),
        Vector2(5.0, 9.0), Vector2(0.0, 18.0), Vector2(-5.0, 9.0)
    ])
    casing.color = Color(0.98, 0.99, 0.99)
    rider_marker.add_child(casing)

    var body: Polygon2D = Polygon2D.new()
    body.polygon = PackedVector2Array([
        Vector2(-7.0, 8.0), Vector2(0.0, -13.0), Vector2(7.0, 8.0),
        Vector2(3.0, 6.0), Vector2(0.0, 12.0), Vector2(-3.0, 6.0)
    ])
    body.color = COLOR_ROUTE
    rider_marker.add_child(body)


func _stat_label(parent: Control, left: float, right: float) -> Label:
    var label: Label = Label.new()
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
    var button: Button = Button.new()
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


func _hide_controls() -> void:
    if controls_panel != null:
        controls_panel.visible = false


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
    var rider_position: Vector3 = _sample_position(travel_m)
    var heading: Vector3 = _smoothed_route_heading(travel_m)
    if heading.length_squared() < 0.001:
        heading = Vector3(0.0, 0.0, -1.0)

    if immediate:
        camera_forward = heading
    else:
        var heading_alpha: float = 1.0 - exp(-delta * HEADING_RESPONSE)
        camera_forward = camera_forward.lerp(heading, heading_alpha).normalized()

    rider_world.global_position = rider_position + Vector3.UP * 0.05

    var target_scale: float = _target_camera_scale()
    if overview_mode:
        target_scale *= 1.55
    if immediate:
        camera_scale = target_scale
    else:
        var zoom_alpha: float = 1.0 - exp(-delta * ZOOM_RESPONSE)
        camera_scale = lerpf(camera_scale, target_scale, zoom_alpha)

    var view_forward: Vector3 = camera_forward if heading_up else Vector3(0.0, 0.0, -1.0)
    var camera_back: float = BASE_CAMERA_BACK_M * camera_scale
    var camera_height: float = BASE_CAMERA_HEIGHT_M * camera_scale
    var lookahead: float = BASE_LOOKAHEAD_M * camera_scale
    var desired_camera: Vector3 = rider_position - view_forward * camera_back + Vector3.UP * camera_height

    if immediate:
        camera.global_position = desired_camera
    else:
        var camera_alpha: float = 1.0 - exp(-delta * CAMERA_RESPONSE)
        camera.global_position = camera.global_position.lerp(desired_camera, camera_alpha)

    var target_fov: float = 54.0 if overview_mode else BASE_FOV_DEG
    camera.fov = target_fov if immediate else lerpf(camera.fov, target_fov, 1.0 - exp(-delta * 3.0))
    camera.look_at(rider_position + view_forward * lookahead, Vector3.UP)

    _update_rider_marker(rider_position, heading)
    _update_hud()

    telemetry_elapsed_s += delta
    if telemetry_elapsed_s >= 1.0:
        telemetry_elapsed_s = 0.0
        var viewport_size: Vector2 = get_viewport().get_visible_rect().size
        var rider_screen: Vector2 = camera.unproject_position(rider_position)
        var rider_y_ratio: float = rider_screen.y / viewport_size.y if viewport_size.y > 0.0 else 0.0
        print("OPENRIDE_GODOT_CAMERA rider_y_pct=%.3f target=%.3f scale=%.3f maneuver_m=%.1f" % [rider_y_ratio, RIDER_ANCHOR_Y, camera_scale, _next_maneuver_distance_m()])


func _update_rider_marker(rider_position: Vector3, heading: Vector3) -> void:
    if rider_marker == null:
        return
    var screen_position: Vector2 = camera.unproject_position(rider_position + Vector3.UP * 0.05)
    rider_marker.position = screen_position
    if heading_up:
        rider_marker.rotation = 0.0
    else:
        rider_marker.rotation = atan2(heading.x, -heading.z)


func _target_camera_scale() -> float:
    # Preserve camera geometry ratios so speed zoom changes scale without moving
    # the rider anchor. 60 km/h maps to 1.0; slow city riding is closer and
    # motorway speeds progressively show more context.
    var speed_ratio: float = clampf(SPEED_KPH / 120.0, 0.0, 1.0)
    var scale: float = lerpf(0.78, 1.22, speed_ratio)
    var maneuver_distance: float = _next_maneuver_distance_m()
    if maneuver_distance < 65.0:
        scale *= 0.86
    elif maneuver_distance < 170.0:
        scale *= 0.94
    return clampf(scale, 0.70, 1.30)


func _update_hud() -> void:
    if primary_label == null:
        return

    var next_maneuver: Dictionary = _next_maneuver(travel_m)
    if next_maneuver.is_empty():
        primary_label.text = "CONTINUER"
        instruction_label.text = "Suivre l'itinéraire"
        maneuver_symbol.text = "↑"
    else:
        var maneuver_at_m: float = float(next_maneuver["distance"])
        var distance_m: float = maxf(0.0, maneuver_at_m - travel_m)
        if distance_m < 1000.0:
            primary_label.text = "DANS %.0f m" % distance_m
        else:
            primary_label.text = "DANS %.1f km" % (distance_m / 1000.0)
        instruction_label.text = String(next_maneuver["text"])
        maneuver_symbol.text = String(next_maneuver["symbol"])

    var remaining: float = maxf(0.0, route_length - travel_m)
    speed_label.text = "%.0f km/h" % SPEED_KPH
    remaining_label.text = "%.1f km" % (remaining / 1000.0)
    arrival_label.text = _arrival_clock(remaining / SPEED_MPS)


func _arrival_clock(seconds_from_now: float) -> String:
    var now: Dictionary = Time.get_time_dict_from_system()
    var current_minutes: int = int(now["hour"]) * 60 + int(now["minute"])
    var arrival_minutes: int = current_minutes + int(ceil(seconds_from_now / 60.0))
    arrival_minutes %= 24 * 60
    var arrival_hour: int = int(arrival_minutes / 60)
    var arrival_minute: int = arrival_minutes % 60
    return "%02d:%02d" % [arrival_hour, arrival_minute]


func _rebuild_route_metrics() -> void:
    route_cumulative = PackedFloat32Array()
    route_cumulative.resize(route_points.size())
    if route_points.is_empty():
        route_length = 0.0
        return

    route_cumulative[0] = 0.0
    route_length = 0.0
    for i in range(1, route_points.size()):
        route_length += route_points[i - 1].distance_to(route_points[i])
        route_cumulative[i] = route_length

    maneuvers.clear()
    for i in range(1, route_control_points.size() - 1):
        var before: Vector3 = (route_control_points[i] - route_control_points[i - 1]).normalized()
        var after: Vector3 = (route_control_points[i + 1] - route_control_points[i]).normalized()
        var angle: float = acos(clampf(before.dot(after), -1.0, 1.0))
        if angle < deg_to_rad(20.0):
            continue
        var cross_y: float = before.cross(after).y
        var right_turn: bool = cross_y > 0.0
        var nearest_distance: float = _distance_for_nearest_route_point(route_control_points[i])
        maneuvers.append({
            "distance": nearest_distance,
            "text": "Tourner à droite" if right_turn else "Tourner à gauche",
            "symbol": "↱" if right_turn else "↰"
        })


func _distance_for_nearest_route_point(point: Vector3) -> float:
    var best_distance_sq: float = INF
    var best_index: int = 0
    for i in range(route_points.size()):
        var distance_sq: float = route_points[i].distance_squared_to(point)
        if distance_sq < best_distance_sq:
            best_distance_sq = distance_sq
            best_index = i
    return float(route_cumulative[best_index])


func _next_maneuver(distance_m: float) -> Dictionary:
    for i in range(maneuvers.size()):
        var maneuver: Dictionary = maneuvers[i]
        if float(maneuver["distance"]) > distance_m + 4.0:
            return maneuver
    return {}


func _next_maneuver_distance_m() -> float:
    var maneuver: Dictionary = _next_maneuver(travel_m)
    if maneuver.is_empty():
        return 9999.0
    return maxf(0.0, float(maneuver["distance"]) - travel_m)


func _sample_position(distance_m: float) -> Vector3:
    if route_points.size() < 2:
        return Vector3.ZERO
    var d: float = clampf(distance_m, 0.0, route_length)
    for i in range(route_points.size() - 1):
        var start_d: float = float(route_cumulative[i])
        var end_d: float = float(route_cumulative[i + 1])
        if d <= end_d or i == route_points.size() - 2:
            var span: float = maxf(0.001, end_d - start_d)
            var t: float = clampf((d - start_d) / span, 0.0, 1.0)
            return route_points[i].lerp(route_points[i + 1], t)
    return route_points[route_points.size() - 1]


func _smoothed_route_heading(distance_m: float) -> Vector3:
    var behind: Vector3 = _sample_position(maxf(0.0, distance_m - 9.0))
    var ahead: Vector3 = _sample_position(minf(route_length, distance_m + 14.0))
    var direction: Vector3 = ahead - behind
    direction.y = 0.0
    if direction.length_squared() < 0.0001:
        return Vector3(0.0, 0.0, -1.0)
    return direction.normalized()


func _chaikin_smooth(points: PackedVector3Array, iterations: int) -> PackedVector3Array:
    if points.size() < 3 or iterations <= 0:
        return points
    var result: PackedVector3Array = points
    for _iteration in range(iterations):
        var next: PackedVector3Array = PackedVector3Array()
        next.append(result[0])
        for i in range(result.size() - 1):
            var a: Vector3 = result[i]
            var b: Vector3 = result[i + 1]
            next.append(a * 0.75 + b * 0.25)
            next.append(a * 0.25 + b * 0.75)
        next.append(result[result.size() - 1])
        result = next
    return result


func _material(color: Color) -> StandardMaterial3D:
    var material: StandardMaterial3D = StandardMaterial3D.new()
    material.albedo_color = color
    material.roughness = 1.0
    material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
    return material


func _polyline_mesh(points: PackedVector3Array, width: float, color: Color, y_offset: float) -> MeshInstance3D:
    var instance: MeshInstance3D = MeshInstance3D.new()
    if points.size() < 2:
        return instance

    var left: PackedVector3Array = PackedVector3Array()
    var right: PackedVector3Array = PackedVector3Array()
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
        var side: Vector3 = Vector3(-direction.z, 0.0, direction.x)
        var elevated: Vector3 = points[i] + Vector3.UP * y_offset
        left[i] = elevated + side * width * 0.5
        right[i] = elevated - side * width * 0.5

    var surface: SurfaceTool = SurfaceTool.new()
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
