from tensometer import units


def test_steps_mm_round_trip():
    assert units.steps_to_mm(units.mm_to_steps(12.5)) == 12.5


def test_mm_to_steps_matches_known_constant():
    # 1 mm at 2000 steps/mm (DTGN 5:1 + 4 mm lead + 1/8 microstep).
    assert units.mm_to_steps(1.0) == 2000


def test_mm_um_round_trip():
    assert units.um_to_mm(units.mm_to_um(45.678)) == 45.678


def test_n_mN_round_trip():
    assert units.mN_to_n(units.n_to_mN(12.345)) == 12.345


def test_percent_permille_round_trip():
    assert units.permille_to_percent(units.percent_to_permille(5.5)) == 5.5


def test_mm_to_um_rounds_to_nearest_integer():
    assert units.mm_to_um(1.0001) == 1000  # 1000.1 um rounds down
    assert units.mm_to_um(1.0009) == 1001  # 1000.9 um rounds up
