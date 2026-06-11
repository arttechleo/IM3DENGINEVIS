import unreal
ss=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in ss.get_all_level_actors():
    if a.get_class().get_name()=="PanoramicCapture360":
        comps=a.get_components_by_class(unreal.SceneCaptureComponent2D)
        print("RIG", a.get_actor_label(), "comp_count=", len(comps))
        for c in comps:
            r=c.relative_rotation
            print("COMP %s pitch=%.1f yaw=%.1f roll=%.1f" % (c.get_name(), r.pitch, r.yaw, r.roll))
