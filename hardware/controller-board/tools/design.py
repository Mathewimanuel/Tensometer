"""Single source of truth for the controller board: parts, footprints and pin->net map.

gen_sch.py turns this into the schematic; gen_pcb.py builds the board from the netlist
that KiCad itself exports from that schematic (so the PCB follows the schematic, not
this file directly). Pin assignments follow firmware/include/config.h and the firmware
skill section 2.6. Net names ending in _C are the cable side of a filter/series resistor.
"""

# (ref, lib_id, value, footprint, {pin_number: net}, group)
# A net of None means "no connect" (gets an X in the schematic).

FP_R = "Resistor_SMD:R_0805_2012Metric"
FP_R1206 = "Resistor_SMD:R_1206_3216Metric"
FP_C = "Capacitor_SMD:C_0805_2012Metric"
FP_C1206 = "Capacitor_SMD:C_1206_3216Metric"
FP_C1210 = "Capacitor_SMD:C_1210_3225Metric"
FP_XH2 = "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical"
FP_XH4 = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
FP_TB2 = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal"
FP_TB6 = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-6-5.08_1x06_P5.08mm_Horizontal"


def R(ref, value, a, b, group, fp=FP_R):
    return (ref, "Device:R", value, fp, {"1": a, "2": b}, group)


def C(ref, value, a, b, group, fp=FP_C):
    return (ref, "Device:C", value, fp, {"1": a, "2": b}, group)


# Teensy 4.1 physical pin order (datasheet pinout, USB end at the top).
# Left column top->bottom = footprint pads 1..24, right column bottom->top = 25..48.
TEENSY_LEFT = ["GND", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
               "3V3", "24", "25", "26", "27", "28", "29", "30", "31", "32"]
TEENSY_RIGHT_TOP_DOWN = ["VIN", "GND", "3V3", "23", "22", "21", "20", "19", "18", "17", "16",
                         "15", "14", "13", "GND", "41", "40", "39", "38", "37", "36", "35",
                         "34", "33"]
TEENSY_RIGHT = list(reversed(TEENSY_RIGHT_TOP_DOWN))  # pads 25..48

# Teensy signal name -> net. Anything not listed is left unconnected.
TEENSY_NETS = {
    "GND": "GND", "VIN": "VIN_T", "3V3": "+3V3",
    "2": "STEP", "3": "DIR", "4": "ENA",
    "5": "HX_DOUT", "6": "HX_SCK",
    "7": "LIM_NEAR", "8": "LIM_FAR", "9": "ESTOP_SENSE",
    "18": "I2C_SDA", "19": "I2C_SCL",
    "16": "ADC_A0", "17": "ADC_A1", "20": "ADC_A2", "21": "ADC_A3",
    "22": "ADC_B0", "23": "ADC_B1", "24": "ADC_B2", "25": "ADC_B3", "26": "V_EXC_SENSE",
    "29": "SYNC_LED", "30": "BTN_START", "31": "BTN_STOP", "32": "BTN_JOG_POS",
    "33": "BTN_JOG_NEG", "34": "BTN_HOME", "35": "BTN_TARE", "36": "FAULT_LED",
}


def teensy_pins():
    pins = {}
    for i, name in enumerate(TEENSY_LEFT + TEENSY_RIGHT, start=1):
        pins[str(i)] = TEENSY_NETS.get(name)
    return pins


PARTS = [
    # ---------------- Power: 24 V in -> fuse -> reverse protection -> 5 V buck -> Teensy
    ("J1", "Connector_Generic:Conn_01x02", "24V_IN", FP_TB2, {"1": "+24V", "2": "GND"}, "power"),
    ("F1", "Device:Polyfuse", "0.5A", "Fuse:Fuse_1812_4532Metric", {"1": "+24V", "2": "24V_F"}, "power"),
    ("D1", "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA", {"1": "VIN_24", "2": "24V_F"}, "power"),
    ("D2", "Device:D_TVS", "SMBJ30CA", "Diode_SMD:D_SMB", {"1": "VIN_24", "2": "GND"}, "power"),
    C("C1", "10u 50V", "VIN_24", "GND", "power", FP_C1210),
    ("U1", "Regulator_Switching:R-78E5.0-0.5", "R-78E5.0-1.0",
     "Converter_DCDC:Converter_DCDC_RECOM_R-78E-0.5_THT",
     {"1": "VIN_24", "2": "GND", "3": "+5V"}, "power"),
    C("C2", "22u 10V", "+5V", "GND", "power", FP_C1206),
    R("R1", "2k2", "+5V", "PWR_LED_A", "power"),
    ("D3", "Device:LED", "GREEN", "LED_SMD:LED_0805_2012Metric", {"1": "GND", "2": "PWR_LED_A"}, "power"),
    ("D4", "Device:D_Schottky", "SS14", "Diode_SMD:D_SMA", {"1": "VIN_T", "2": "+5V"}, "power"),
    C("C3", "10u", "VIN_T", "GND", "power"),

    # ---------------- Teensy 4.1
    ("U2", "tensometer:Teensy4.1", "Teensy 4.1", "tensometer:Teensy41_Socket", teensy_pins(), "mcu"),

    # ---------------- Driver interface: 74AHCT244 at 5 V, two gates per signal
    ("U3", "74xx:74AHCT244", "74AHCT244", "Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm", {
        "1": "GND", "19": "GND",                 # both output enables permanently active
        "2": "STEP", "4": "STEP", "18": "PUL_N", "16": "PUL_N",
        "6": "DIR", "8": "DIR", "14": "DIR_N", "12": "DIR_N",
        "11": "ENA", "13": "ENA", "9": "ENA_N", "7": "ENA_N",
        "15": "GND", "17": "GND", "5": None, "3": None,   # spare gates: inputs grounded
        "10": "GND", "20": "+5V"}, "driver"),
    C("C4", "100n", "+5V", "GND", "driver"),
    R("R2", "10k", "+3V3", "STEP", "driver"),    # STEP idles HIGH = opto off
    R("R3", "10k", "+3V3", "DIR", "driver"),
    R("R4", "10k", "ENA", "GND", "driver"),      # ENA LOW = opto on = TB6600 DISABLED
    ("J2", "Connector_Generic:Conn_01x06", "TB6600", FP_TB6, {
        "1": "+5V", "2": "PUL_N", "3": "+5V", "4": "DIR_N", "5": "+5V", "6": "ENA_N"}, "driver"),

    # ---------------- HX711 (module sits on the carriage; 4 wires through the drag chain)
    ("U4", "Regulator_Linear:TLV75533PDBV", "TLV75533", "Package_TO_SOT_SMD:SOT-23-5",
     {"1": "+5V", "2": "GND", "3": "+5V", "4": None, "5": "+3.3VA"}, "hx711"),
    C("C5", "1u", "+5V", "GND", "hx711"),
    C("C6", "10u", "+3.3VA", "GND", "hx711"),
    R("R5", "100", "HX_DOUT_C", "HX_DOUT", "hx711"),
    R("R6", "100", "HX_SCK", "HX_SCK_C", "hx711"),
    ("J3", "Connector_Generic:Conn_01x04", "HX711", FP_XH4,
     {"1": "+3.3VA", "2": "GND", "3": "HX_DOUT_C", "4": "HX_SCK_C"}, "hx711"),

    # ---------------- Limit switches (NC to GND; open or broken wire reads HIGH = trip)
    ("J4", "Connector_Generic:Conn_01x02", "LIM_NEAR", FP_XH2, {"1": "LIM_NEAR_C", "2": "GND"}, "limits"),
    R("R7", "2k2", "+3V3", "LIM_NEAR_C", "limits"),
    R("R8", "1k", "LIM_NEAR_C", "LIM_NEAR", "limits"),
    C("C7", "100n", "LIM_NEAR", "GND", "limits"),
    ("J5", "Connector_Generic:Conn_01x02", "LIM_FAR", FP_XH2, {"1": "LIM_FAR_C", "2": "GND"}, "limits"),
    R("R9", "2k2", "+3V3", "LIM_FAR_C", "limits"),
    R("R10", "1k", "LIM_FAR_C", "LIM_FAR", "limits"),
    C("C8", "100n", "LIM_FAR", "GND", "limits"),

    # ---------------- E-stop sense: 24 V after the contactor -> opto -> pin LOW
    # 24 V present = LOW (normal). E-stop pressed or wire broken = HIGH (estop_active).
    ("J6", "Connector_Generic:Conn_01x02", "ESTOP_SENSE", FP_TB2, {"1": "24V_SW", "2": "GND"}, "estop"),
    R("R11", "10k", "24V_SW", "OPTO_A", "estop", FP_R1206),
    ("D5", "Diode:1N4148W", "1N4148W", "Diode_SMD:D_SOD-123", {"1": "OPTO_A", "2": "GND"}, "estop"),
    ("U5", "Isolator:PC817", "PC817", "Package_DIP:DIP-4_W7.62mm",
     {"1": "OPTO_A", "2": "GND", "3": "GND", "4": "ESTOP_SENSE"}, "estop"),
    R("R12", "10k", "+3V3", "ESTOP_SENSE", "estop"),
    C("C9", "100n", "ESTOP_SENSE", "GND", "estop"),

    # ---------------- Operator panel (buttons to GND, ribbon cable)
    ("J7", "Connector_Generic:Conn_02x05_Odd_Even", "PANEL", "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical", {
        "1": "BTN_START", "2": "BTN_STOP", "3": "BTN_JOG_POS", "4": "BTN_JOG_NEG",
        "5": "BTN_HOME", "6": "BTN_TARE", "7": "FAULT_LED_C", "8": "+3V3", "9": "GND", "10": "GND"},
     "panel"),
    R("R13", "10k", "+3V3", "BTN_START", "panel"), C("C10", "100n", "BTN_START", "GND", "panel"),
    R("R14", "10k", "+3V3", "BTN_STOP", "panel"), C("C11", "100n", "BTN_STOP", "GND", "panel"),
    R("R15", "10k", "+3V3", "BTN_JOG_POS", "panel"), C("C12", "100n", "BTN_JOG_POS", "GND", "panel"),
    R("R16", "10k", "+3V3", "BTN_JOG_NEG", "panel"), C("C13", "100n", "BTN_JOG_NEG", "GND", "panel"),
    R("R17", "10k", "+3V3", "BTN_HOME", "panel"), C("C14", "100n", "BTN_HOME", "GND", "panel"),
    R("R18", "10k", "+3V3", "BTN_TARE", "panel"), C("C15", "100n", "BTN_TARE", "GND", "panel"),
    R("R19", "330", "FAULT_LED", "FAULT_LED_C", "panel"),   # ~4 mA, under the 10 mA pin limit

    # ---------------- OLED (SSD1306 I2C module)
    ("J8", "Connector_Generic:Conn_01x04", "OLED", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
     {"1": "GND", "2": "+3V3", "3": "I2C_SCL", "4": "I2C_SDA"}, "oled"),
    R("R20", "4k7", "+3V3", "I2C_SCL", "oled"),
    R("R21", "4k7", "+3V3", "I2C_SDA", "oled"),

    # ---------------- Video sync LED (bright LED off-board, low-side MOSFET)
    ("Q1", "Transistor_FET:AO3400A", "AO3400A", "Package_TO_SOT_SMD:SOT-23",
     {"1": "SYNC_G", "2": "GND", "3": "SYNC_D"}, "sync"),
    R("R22", "100", "SYNC_LED", "SYNC_G", "sync"),
    R("R23", "100k", "SYNC_G", "GND", "sync"),
    R("R24", "100", "SYNC_LED_C", "SYNC_D", "sync"),      # ~20 mA for a white LED from 5 V
    ("J9", "Connector_Generic:Conn_01x02", "SYNC_LED", FP_XH2, {"1": "+5V", "2": "SYNC_LED_C"}, "sync"),

    # ---------------- Phase 2 expansion header (continuity / routing board)
    ("J10", "Connector_Generic:Conn_02x08_Odd_Even", "EXPANSION", "Connector_PinHeader_2.54mm:PinHeader_2x08_P2.54mm_Vertical", {
        "1": "ADC_A0", "2": "ADC_A1", "3": "ADC_A2", "4": "ADC_A3",
        "5": "ADC_B0", "6": "ADC_B1", "7": "ADC_B2", "8": "ADC_B3",
        "9": "V_EXC_SENSE", "10": "GND", "11": "I2C_SDA", "12": "I2C_SCL",
        "13": "+3V3", "14": "+5V", "15": "GND", "16": "GND"}, "expansion"),

    # ---------------- Mounting
    ("H1", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    ("H2", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    ("H3", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    ("H4", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
]

# Nets drawn with power symbols (everything else gets a net label).
POWER_NETS = {"+24V": "power:+24V", "+5V": "power:+5V", "+3V3": "power:+3V3",
              "+3.3VA": "power:+3.3VA", "GND": "power:GND"}

# Nets that need a PWR_FLAG (driven only through passive parts, but feed power inputs).
PWR_FLAG_NETS = ["+24V", "GND", "VIN_24", "VIN_T"]

GROUP_TITLES = {
    "power": "24 V input, protection, 5 V buck, Teensy VIN",
    "mcu": "Teensy 4.1 (socketed). Cut the VUSB-VIN pad on the Teensy.",
    "driver": "TB6600 interface: 74AHCT244 at 5 V, reset-safe pulls (issue #18)",
    "hx711": "HX711: dedicated 3.3 V LDO, cable to the carriage",
    "limits": "Limit switches (NC, fail-safe)",
    "estop": "E-stop sense: 24 V after the contactor",
    "panel": "Operator panel (ribbon)",
    "oled": "OLED (I2C)",
    "sync": "Video sync LED driver",
    "expansion": "Phase 2 expansion header",
    "mech": "Mounting",
}


def intended_nets():
    """net -> sorted list of 'REF.pin' (for checking the KiCad-exported netlist)."""
    nets = {}
    for ref, _lib, _val, _fp, pins, _grp in PARTS:
        for pin, net in pins.items():
            if net is not None:
                nets.setdefault(net, []).append(f"{ref}.{pin}")
    return {n: sorted(v) for n, v in nets.items()}
