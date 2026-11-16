from tensometer.gui.dialogs import ArmConfigDialog, MetadataDialog


def test_metadata_dialog_result_fields(qtbot):
    dlg = MetadataDialog(default_l0_mm=50.0)
    qtbot.addWidget(dlg)
    dlg.test_name_edit.setText("fabric01")
    dlg.material_combo.setCurrentText("Knit fabric")
    dlg.width_spin.setValue(10.0)
    dlg.thickness_spin.setValue(0.5)
    dlg.notes_edit.setPlainText("first sample")

    fields = dlg.result_fields()
    assert fields["test_name"] == "fabric01"
    assert fields["material"] == "Knit fabric"
    assert fields["l0_mm"] == 50.0
    assert fields["specimen_width_mm"] == 10.0
    assert fields["specimen_thickness_mm"] == 0.5
    assert fields["operator_notes"] == "first sample"


def test_metadata_dialog_defaults_test_name_when_blank(qtbot):
    dlg = MetadataDialog()
    qtbot.addWidget(dlg)
    assert dlg.result_fields()["test_name"] == "test"


def test_metadata_dialog_blank_dimensions_become_none(qtbot):
    dlg = MetadataDialog()
    qtbot.addWidget(dlg)
    fields = dlg.result_fields()
    assert fields["specimen_width_mm"] is None
    assert fields["specimen_thickness_mm"] is None


def test_arm_config_dialog_run_mode(qtbot):
    dlg = ArmConfigDialog(l0_mm=50.0)
    qtbot.addWidget(dlg)
    dlg.speed_spin.setValue(100.0)
    dlg.max_travel_spin.setValue(30.0)
    dlg.force_limit_spin.setValue(880.0)

    config = dlg.arm_config()
    assert config.l0_mm == 50.0
    assert config.speed_mm_per_min == 100.0
    assert config.max_travel_mm == 30.0
    assert config.force_limit_N == 880.0
    assert config.cycle_mode is False


def test_arm_config_dialog_cycle_fields_disabled_until_checked(qtbot):
    dlg = ArmConfigDialog()
    qtbot.addWidget(dlg)
    assert dlg.cycle_min_spin.isEnabled() is False
    assert dlg.cycle_max_spin.isEnabled() is False
    assert dlg.cycle_count_spin.isEnabled() is False

    dlg.cycle_mode_checkbox.setChecked(True)
    assert dlg.cycle_min_spin.isEnabled() is True
    assert dlg.cycle_max_spin.isEnabled() is True
    assert dlg.cycle_count_spin.isEnabled() is True


def test_arm_config_dialog_cycle_mode(qtbot):
    dlg = ArmConfigDialog()
    qtbot.addWidget(dlg)
    dlg.cycle_mode_checkbox.setChecked(True)
    dlg.cycle_min_spin.setValue(5.0)
    dlg.cycle_max_spin.setValue(60.0)
    dlg.cycle_count_spin.setValue(10_000)

    config = dlg.arm_config()
    assert config.cycle_mode is True
    assert config.cycle_strain_min_percent == 5.0
    assert config.cycle_strain_max_percent == 60.0
    assert config.cycle_count == 10_000
