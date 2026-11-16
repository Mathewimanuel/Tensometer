"""ARM-config and specimen-metadata dialogs: kept as two separate dialogs, not one
combined form (docs/decisions.md GUI-layout entry, 2026-11-16) -- metadata describes the
specimen, the ARM dialog configures how to run the test against it.
"""

from __future__ import annotations

from PySide6 import QtWidgets

from ..controller import ArmConfig


class MetadataDialog(QtWidgets.QDialog):
    """Collects specimen-level facts (docs/decisions.md plotting-design entry):
    material, L0, dimensions, operator notes. Does NOT collect arm_config or
    started_at_wall_clock -- those are filled in by the caller once the ArmConfigDialog
    (below) and the actual test start are known, combined into a full
    datalog.TestMetadata only then."""

    def __init__(self, parent=None, default_l0_mm: float = 0.0) -> None:
        super().__init__(parent)
        self.setWindowTitle("Specimen metadata")
        form = QtWidgets.QFormLayout(self)

        self.test_name_edit = QtWidgets.QLineEdit()
        form.addRow("Test name", self.test_name_edit)

        self.material_combo = QtWidgets.QComboBox()
        self.material_combo.setEditable(True)
        self.material_combo.addItems(["Knit fabric", "ABS", "Neoprene"])
        form.addRow("Material", self.material_combo)

        self.l0_spin = QtWidgets.QDoubleSpinBox()
        self.l0_spin.setRange(0, 1000)
        self.l0_spin.setSuffix(" mm")
        self.l0_spin.setValue(default_l0_mm)
        form.addRow("Gauge length (L0)", self.l0_spin)

        self.width_spin = QtWidgets.QDoubleSpinBox()
        self.width_spin.setRange(0, 500)
        self.width_spin.setSuffix(" mm")
        form.addRow("Specimen width", self.width_spin)

        self.thickness_spin = QtWidgets.QDoubleSpinBox()
        self.thickness_spin.setRange(0, 100)
        self.thickness_spin.setDecimals(3)
        self.thickness_spin.setSuffix(" mm")
        form.addRow("Specimen thickness", self.thickness_spin)

        self.notes_edit = QtWidgets.QPlainTextEdit()
        form.addRow("Operator notes", self.notes_edit)

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Ok
            | QtWidgets.QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def result_fields(self) -> dict:
        """Partial metadata fields -- test_name, material, l0_mm, specimen_width_mm,
        specimen_thickness_mm, operator_notes. The caller fills in the rest
        (arm_config, started_at_wall_clock, cell_profile) once known."""
        return {
            "test_name": self.test_name_edit.text() or "test",
            "material": self.material_combo.currentText(),
            "l0_mm": self.l0_spin.value(),
            "specimen_width_mm": self.width_spin.value() or None,
            "specimen_thickness_mm": self.thickness_spin.value() or None,
            "operator_notes": self.notes_edit.toPlainText(),
        }


class ArmConfigDialog(QtWidgets.QDialog):
    """Configures CMD_ARM's body (controller.ArmConfig) -- RUN or CYCLE mode. Cycle
    fields are only enabled (and only meaningful) once "CYCLE mode" is checked, mirroring
    ArmBody's own "always on the wire, only validated when cycle_mode == 1" contract."""

    def __init__(self, parent=None, l0_mm: float = 0.0) -> None:
        super().__init__(parent)
        self.setWindowTitle("Arm test")
        form = QtWidgets.QFormLayout(self)

        self.l0_spin = QtWidgets.QDoubleSpinBox()
        self.l0_spin.setRange(0, 1000)
        self.l0_spin.setSuffix(" mm")
        self.l0_spin.setValue(l0_mm)
        form.addRow("Gauge length (L0)", self.l0_spin)

        self.speed_spin = QtWidgets.QDoubleSpinBox()
        self.speed_spin.setRange(0, 500)
        self.speed_spin.setSuffix(" mm/min")
        self.speed_spin.setValue(200)
        form.addRow("Speed", self.speed_spin)

        self.max_travel_spin = QtWidgets.QDoubleSpinBox()
        self.max_travel_spin.setRange(0, 500)
        self.max_travel_spin.setSuffix(" mm")
        form.addRow("Max travel (soft limit)", self.max_travel_spin)

        self.force_limit_spin = QtWidgets.QDoubleSpinBox()
        self.force_limit_spin.setRange(0, 10_000)
        self.force_limit_spin.setSuffix(" N")
        self.force_limit_spin.setSpecialValueText("disabled (profile cutoff only)")
        form.addRow("Force limit", self.force_limit_spin)

        self.strain_limit_spin = QtWidgets.QDoubleSpinBox()
        self.strain_limit_spin.setRange(0, 1000)
        self.strain_limit_spin.setSuffix(" %")
        self.strain_limit_spin.setSpecialValueText("disabled")
        form.addRow("Strain limit", self.strain_limit_spin)

        self.cycle_mode_checkbox = QtWidgets.QCheckBox("CYCLE mode (fatigue test)")
        form.addRow(self.cycle_mode_checkbox)

        self.cycle_min_spin = QtWidgets.QDoubleSpinBox()
        self.cycle_min_spin.setRange(0, 1000)
        self.cycle_min_spin.setSuffix(" %")
        form.addRow("Cycle floor strain", self.cycle_min_spin)

        self.cycle_max_spin = QtWidgets.QDoubleSpinBox()
        self.cycle_max_spin.setRange(0, 1000)
        self.cycle_max_spin.setSuffix(" %")
        form.addRow("Cycle peak strain", self.cycle_max_spin)

        self.cycle_count_spin = QtWidgets.QSpinBox()
        self.cycle_count_spin.setRange(0, 1_000_000)
        form.addRow("Cycle count", self.cycle_count_spin)

        self._cycle_fields = [self.cycle_min_spin, self.cycle_max_spin, self.cycle_count_spin]
        self.cycle_mode_checkbox.toggled.connect(self._on_cycle_mode_toggled)
        self._on_cycle_mode_toggled(False)

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Ok
            | QtWidgets.QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def _on_cycle_mode_toggled(self, checked: bool) -> None:
        for field in self._cycle_fields:
            field.setEnabled(checked)

    def arm_config(self) -> ArmConfig:
        return ArmConfig(
            l0_mm=self.l0_spin.value(),
            speed_mm_per_min=self.speed_spin.value(),
            max_travel_mm=self.max_travel_spin.value(),
            force_limit_N=self.force_limit_spin.value(),
            strain_limit_percent=self.strain_limit_spin.value(),
            cycle_mode=self.cycle_mode_checkbox.isChecked(),
            cycle_strain_min_percent=self.cycle_min_spin.value(),
            cycle_strain_max_percent=self.cycle_max_spin.value(),
            cycle_count=self.cycle_count_spin.value(),
        )
