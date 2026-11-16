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
FP_RN = "Resistor_SMD:R_Array_Convex_4x0603"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"


def R(ref, value, a, b, group, fp=FP_R):
    return (ref, "Device:R", value, fp, {"1": a, "2": b}, group)


def C(ref, value, a, b, group, fp=FP_C):
    return (ref, "Device:C", value, fp, {"1": a, "2": b}, group)


def RN(ref, value, pairs, group):
    """4-resistor array (0603x4, convex). pairs: up to 4 (a, b) net pairs; element n
    sits between pins n and 9-n. Unused elements are left unconnected."""
    pins = {}
    for n in range(1, 5):
        a, b = pairs[n - 1] if n <= len(pairs) else (None, None)
        pins[str(n)], pins[str(9 - n)] = a, b
    return (ref, "Device:R_Pack04", value, FP_RN, pins, group)


def TP(ref, net):
    return (ref, "Connector:TestPoint", net, FP_TP, {"1": net}, "test")


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
    ("F1", "Device:Polyfuse", "0.5A 60V", "Fuse:Fuse_1812_4532Metric", {"1": "+24V", "2": "24V_F"}, "power"),
    ("D1", "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA", {"1": "VIN_24", "2": "24V_F"}, "power"),
    ("D2", "Device:D_Zener", "SMBJ28A", "Diode_SMD:D_SMB", {"1": "VIN_24", "2": "GND"}, "power"),  # unidirectional TVS
    C("C1", "10u 50V", "VIN_24", "GND", "power", FP_C1206),
    # Bulk input cap: damps cable/contactor ringing that a ceramic alone would not.
    ("C16", "Device:C_Polarized", "100u 50V", "Capacitor_SMD:CP_Elec_6.3x7.7",
     {"1": "VIN_24", "2": "GND"}, "power"),
    # 9-72 V input (75 V max): the SMBJ28A clamps at up to ~45 V, so the converter must
    # survive well above that (the R-78E it replaces was rated 36 V max). Plain 3-pin
    # SIP, exactly KiCad's footprint (the TRACO TSR 1-WI tried first has extra case pins).
    # 0.5 A: this board draws ~0.25 A, leaving ~0.25 A for phase 2. Max 100 uF on +5V.
    ("U1", "Regulator_Switching:R-78HB5.0-0.5", "R-78HB5.0-0.5",
     "Converter_DCDC:Converter_DCDC_RECOM_R-78HB-0.5_THT",
     {"1": "VIN_24", "2": "GND", "3": "+5V"}, "power"),
    C("C2", "22u 25V", "+5V", "GND", "power"),
    R("R1", "2k2", "+5V", "PWR_LED_A", "power"),
    ("D3", "Device:LED", "GREEN", "LED_SMD:LED_0805_2012Metric", {"1": "GND", "2": "PWR_LED_A"}, "power"),
    ("D4", "Device:D_Schottky", "SS14", "Diode_SMD:D_SMA", {"1": "VIN_T", "2": "+5V"}, "power"),
    C("C3", "10u", "VIN_T", "GND", "power"),

    # ---------------- Teensy 4.1
    ("U2", "tensometer:Teensy4.1", "Teensy 4.1", "tensometer:Teensy41_Socket", teensy_pins(), "mcu"),

    # ---------------- Driver interface: 74AHCT244 at 5 V, two gates per signal
    ("U3", "74xx:74AHCT244", "74AHCT244", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", {
        "1": "GND", "19": "GND",                 # both output enables permanently active
        "2": "STEP", "4": "STEP", "18": "PUL_N", "16": "PUL_N",
        "6": "DIR", "8": "DIR", "14": "DIR_N", "12": "DIR_N",
        "11": "ENA", "13": "ENA", "9": "ENA_N", "7": "ENA_N",
        "15": "GND", "17": "GND", "5": None, "3": None,   # spare gates: inputs grounded
        "10": "GND", "20": "+5V"}, "driver"),
    C("C4", "100n", "+5V", "GND", "driver"),
    # STEP/DIR pull-ups need a 3.3 V that exists whenever +5V does, Teensy plugged in or
    # not (the Teensy's own 3V3 is absent when it is unplugged, not yet up, or off, and
    # then STEP/DIR would sit LOW = opto ON). U6 is a second TLV75533 for just this.
    ("U6", "Regulator_Linear:TLV75533PDBV", "TLV75533", "Package_TO_SOT_SMD:SOT-23-5",
     {"1": "+5V", "2": "GND", "3": "+5V", "4": None, "5": "3V3_PU"}, "driver"),
    C("C17", "1u", "3V3_PU", "GND", "driver"),
    R("R2", "10k", "3V3_PU", "STEP", "driver"),    # STEP idles HIGH = opto off
    R("R3", "10k", "3V3_PU", "DIR", "driver"),
    R("R4", "10k", "ENA", "GND", "driver"),      # ENA LOW = opto on = TB6600 DISABLED
    ("J2", "Connector_Generic:Conn_01x06", "TB6600", "Connector_Phoenix_MSTB:PhoenixContact_MSTBA_2,5_6-G-5,08_1x06_P5.08mm_Horizontal", {
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
    # The LED side has its own return (ESTOP_RTN, to the 24 V supply's 0 V through J6),
    # not board GND: the opto actually isolates, and swapped J6 wires only reverse-bias
    # the LED (D5 then conducts ~2.4 mA through R11) instead of putting 24 V on GND.
    ("J6", "Connector_Generic:Conn_01x02", "ESTOP_SENSE", FP_TB2, {"1": "24V_SW", "2": "ESTOP_RTN"}, "estop"),
    R("R11", "10k", "24V_SW", "OPTO_A", "estop", FP_R1206),
    ("D5", "Diode:1N4148W", "1N4148W", "Diode_SMD:D_SOD-123", {"1": "OPTO_A", "2": "ESTOP_RTN"}, "estop"),
    ("U5", "Isolator:PC817", "LTV-817S", "Package_DIP:SMDIP-4_W9.53mm",
     {"1": "OPTO_A", "2": "ESTOP_RTN", "3": "GND", "4": "ESTOP_SENSE"}, "estop"),
    R("R12", "10k", "+3V3", "ESTOP_SENSE", "estop"),
    C("C9", "100n", "ESTOP_SENSE", "GND", "estop"),

    # ---------------- Operator panel (buttons to GND, ribbon cable)
    # Buttons pull the cable line to GND. At the pin: 10k pull-up and 100n to GND; 1k in
    # series from the cable (ESD / miswiring current limit, and with the 100n a ~0.1 ms
    # RC). A pressed button reads 0.3 V. All of it sits next to J7.
    ("J7", "Connector_Generic:Conn_02x05_Odd_Even", "PANEL", "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical", {
        "1": "BTN_START_C", "2": "BTN_STOP_C", "3": "BTN_JOG_POS_C", "4": "BTN_JOG_NEG_C",
        "5": "BTN_HOME_C", "6": "BTN_TARE_C", "7": "FAULT_LED_C", "8": "+3V3", "9": "GND", "10": "GND"},
     "panel"),
    RN("RN1", "1k", [("BTN_START_C", "BTN_START"), ("BTN_STOP_C", "BTN_STOP"),
                     ("BTN_JOG_POS_C", "BTN_JOG_POS"), ("BTN_JOG_NEG_C", "BTN_JOG_NEG")], "panel"),
    RN("RN2", "1k", [("BTN_HOME_C", "BTN_HOME"), ("BTN_TARE_C", "BTN_TARE")], "panel"),
    RN("RN3", "10k", [("+3V3", "BTN_START"), ("+3V3", "BTN_STOP"),
                      ("+3V3", "BTN_JOG_POS"), ("+3V3", "BTN_JOG_NEG")], "panel"),
    RN("RN4", "10k", [("+3V3", "BTN_HOME"), ("+3V3", "BTN_TARE")], "panel"),
    C("C10", "100n", "BTN_START", "GND", "panel"),
    C("C11", "100n", "BTN_STOP", "GND", "panel"),
    C("C12", "100n", "BTN_JOG_POS", "GND", "panel"),
    C("C13", "100n", "BTN_JOG_NEG", "GND", "panel"),
    C("C14", "100n", "BTN_HOME", "GND", "panel"),
    C("C15", "100n", "BTN_TARE", "GND", "panel"),
    R("R19", "330", "FAULT_LED", "FAULT_LED_C", "panel"),   # ~4 mA, under the 10 mA pin limit

    # ---------------- OLED (SSD1306 I2C module, mounted on the operator panel, cabled)
    # JST-PH (2.0 mm) so its cable cannot go into the HX711's JST-XH 4-way by mistake.
    ("J8", "Connector_Generic:Conn_01x04", "OLED", "Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical",
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
    # Keyed box header (a reversed plug would put +5V on an ADC pin). 1k in series with
    # every analog line: limits current into the Teensy's clamp if the phase 2 board ever
    # drives above 3.3 V (~1.7 mA at 5 V) and is small next to the ADC's input.
    ("J10", "Connector_Generic:Conn_02x08_Odd_Even", "EXPANSION", "Connector_IDC:IDC-Header_2x08_P2.54mm_Vertical", {
        "1": "ADC_A0_C", "2": "ADC_A1_C", "3": "ADC_A2_C", "4": "ADC_A3_C",
        "5": "ADC_B0_C", "6": "ADC_B1_C", "7": "ADC_B2_C", "8": "ADC_B3_C",
        "9": "V_EXC_SENSE_C", "10": "GND", "11": "I2C_SDA_X", "12": "I2C_SCL_X",
        "13": "+3V3", "14": "+5V", "15": "GND", "16": "GND"}, "expansion"),
    RN("RN5", "1k", [(f"ADC_A{i}_C", f"ADC_A{i}") for i in range(4)], "expansion"),
    RN("RN6", "1k", [(f"ADC_B{i}_C", f"ADC_B{i}") for i in range(4)], "expansion"),
    R("R25", "1k", "V_EXC_SENSE_C", "V_EXC_SENSE", "expansion"),
    # 100R on the I2C lines that leave the box (fault current limit, edge damping).
    R("R26", "100", "I2C_SDA_X", "I2C_SDA", "expansion"),
    R("R27", "100", "I2C_SCL_X", "I2C_SCL", "expansion"),

    # ---------------- Test points (bare pads, not assembled): bring-up step 2 and rails
    TP("TP1", "VIN_24"), TP("TP2", "+5V"), TP("TP3", "+3V3"), TP("TP4", "+3.3VA"),
    TP("TP5", "ENA_N"), TP("TP6", "GND"), TP("TP7", "GND"),

    # ---------------- Mounting
    ("H1", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    ("H2", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    ("H3", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    ("H4", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "mech"),
    # Global fiducials for JLC's SMT pick-and-place (fine-pitch TSSOP on board).
    ("FID1", "Mechanical:Fiducial", "Fiducial", "Fiducial:Fiducial_1mm_Mask2mm", {}, "mech"),
    ("FID2", "Mechanical:Fiducial", "Fiducial", "Fiducial:Fiducial_1mm_Mask2mm", {}, "mech"),
    ("FID3", "Mechanical:Fiducial", "Fiducial", "Fiducial:Fiducial_1mm_Mask2mm", {}, "mech"),
]

# JLCPCB / LCSC part numbers, checked against the JLC parts database (jlcparts snapshot,
# stock figures fetched 2026-09-21..10-28). "basic" parts have no JLC extended-part fee.
# THT parts can be assembled by JLC (THT service) or hand-soldered.
LCSC = {
    # resistors (all JLC basic)
    "R1": "C17520", "R7": "C17520", "R9": "C17520",                       # 2k2
    "R2": "C17414", "R3": "C17414", "R4": "C17414", "R12": "C17414",      # 10k
    "R25": "C17513",                                                      # 1k
    "R26": "C17408", "R27": "C17408",                                     # 100R
    "RN1": "C20197", "RN2": "C20197", "RN5": "C20197", "RN6": "C20197",   # 1k x4 0603 array (basic)
    "RN3": "C29718", "RN4": "C29718",                                     # 10k x4 0603 array (basic)
    "R5": "C17408", "R6": "C17408", "R22": "C17408", "R24": "C17408",     # 100R
    "R8": "C17513", "R10": "C17513",                                      # 1k
    "R11": "C17902",                                                      # 10k 1206
    "R19": "C17630",                                                      # 330R
    "R20": "C17673", "R21": "C17673",                                     # 4k7
    "R23": "C149504",                                                     # 100k
    # capacitors (all JLC basic)
    "C1": "C13585",                                                       # 10u 50V 1206
    "C2": "C45783",                                                       # 22u 25V 0805
    "C3": "C15850", "C6": "C15850",                                       # 10u 25V 0805
    "C5": "C28323", "C17": "C28323",                                      # 1u 50V 0805
    "C16": "C47023117",                                                   # 100u 50V electrolytic 6.3x7.7
    "C4": "C49678", "C7": "C49678", "C8": "C49678", "C9": "C49678", "C10": "C49678", "C11": "C49678",
    "C12": "C49678", "C13": "C49678", "C14": "C49678", "C15": "C49678",  # 100n 50V
    # semiconductors
    "D1": "C8678",      # SS34 (basic)
    "D2": "C113997",    # SMBJ28A, MDD (extended)
    "D3": "C2297",      # green 0805 LED (basic)
    "D4": "C2480",      # SS14 (basic)
    "D5": "C81598",     # 1N4148W (basic)
    "F1": "C19078719",  # 1812L050/60GR, 0.5 A / 60 V PTC (extended)
    "Q1": "C20917",     # AO3400A (basic)
    "U1": "C7333563",   # RECOM R-78HB5.0-0.5, 9-72 V in, 5 V 0.5 A, THT (LCSC stock 0 at snapshot: buy at Farnell/RS/Mouser, hand-solder)
    "U3": "C135583",    # 74AHCT244PW,118 TSSOP-20, Nexperia (extended)
    "U4": "C404027", "U6": "C404027",   # TLV75533PDBVR (extended)
    "U5": "C109227",    # LTV-817S-TA1-C (basic)
    # connectors (THT, extended)
    "J1": "C8465", "J6": "C8465",          # WJ500V-5.08-2P screw terminal
    "J2": "C441208",                       # KF2EDGR-5.08-6P header (mating plug KF2EDGK-5.08-6P = C440884)
    "J3": "C144395",                       # JST B4B-XH-A
    "J4": "C158012", "J5": "C158012", "J9": "C158012",  # JST B2B-XH-A
    "J7": "C5665",                         # 2x5 IDC box header
    "J8": "C131334",                       # JST B4B-PH-K-S (OLED cable)
    "J10": "C492443",                      # 2x8 IDC box header, keyed
    # not assembled by JLC: U2 = Teensy 4.1 (yours) in 2x C41376163 1x24 female headers; H1-H4 holes
}

# Manufacturer part numbers for the LCSC parts above (from the same JLC parts snapshot),
# written into the schematic so the BOM is usable outside JLC too.
MPN = {
    "C2297": ("KT-0805G", "Hubei KENTO Elec"),
    "C2480": ("SS14", "MDD Microdiode Semiconductor"),
    "C5665": ("2.54-2*5P简牛", "BOOMELE Boom Precision Elec"),
    "C8465": ("WJ500V-5.08-2P", "Ningbo Kangnex Elec"),
    "C8678": ("SS34", "MDD Microdiode Semiconductor"),
    "C13585": ("CL31A106KBHNNNE", "Samsung Electro Mechanics"),
    "C15850": ("CL21A106KAYNNNE", "Samsung Electro Mechanics"),
    "C17408": ("0805W8F1000T5E", "UNI ROYAL Uniroyal Elec"),
    "C17414": ("0805W8F1002T5E", "UNI ROYAL Uniroyal Elec"),
    "C17513": ("0805W8F1001T5E", "UNI ROYAL Uniroyal Elec"),
    "C17520": ("0805W8F2201T5E", "UNI ROYAL Uniroyal Elec"),
    "C17630": ("0805W8F3300T5E", "UNI ROYAL Uniroyal Elec"),
    "C17673": ("0805W8F4701T5E", "UNI ROYAL Uniroyal Elec"),
    "C17902": ("1206W4F1002T5E", "UNI ROYAL Uniroyal Elec"),
    "C20197": ("4D03WGJ0102T5E", "UNI ROYAL Uniroyal Elec"),
    "C20917": ("AO3400A", "Alpha Omega Semicon"),
    "C28323": ("CL21B105KBFNNNE", "Samsung Electro Mechanics"),
    "C29718": ("4D03WGJ0103T5E", "UNI ROYAL Uniroyal Elec"),
    "C45783": ("CL21A226MAQNNNE", "Samsung Electro Mechanics"),
    "C49678": ("CC0805KRX7R9BB104", "YAGEO"),
    "C81598": ("1N4148W", "ST Semtech"),
    "C109227": ("LTV-817S-TA1-C", "Lite On"),
    "C113997": ("SMBJ28A", "MDD Microdiode Semiconductor"),
    "C131334": ("B4B-PH-K-S(LF)(SN)", "JST"),
    "C135583": ("74AHCT244PW,118", "Nexperia"),
    "C144395": ("B4B-XH-A(LF)(SN)", "JST"),
    "C149504": ("0805W8F1003T5E", "UNI ROYAL Uniroyal Elec"),
    "C158012": ("B2B-XH-A(LF)(SN)", "JST"),
    "C404027": ("TLV75533PDBVR", "Texas Instruments"),
    "C441208": ("KF2EDGR-5.08-6P", "Cixi Kefa Elec"),
    "C492443": ("BH254V-16P", "XFCN"),
    "C7333563": ("R-78HB5.0-0.5", "Recom Power"),
    "C19078719": ("1812L050/60GR", "LUTE"),
    "C47023117": ("RVT50V100M6X8", "jieerrui"),
}

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
    "estop": "E-stop sense: 24 V after the contactor, isolated return",
    "panel": "Operator panel (ribbon): 1k series, 10k pull-up, 100n at the pin",
    "oled": "OLED (I2C)",
    "sync": "Video sync LED driver",
    "expansion": "Phase 2 expansion header (keyed), 1k on analog lines",
    "test": "Test points (bare pads)",
    "mech": "Mounting holes and fiducials",
}


def intended_nets():
    """net -> sorted list of 'REF.pin' (for checking the KiCad-exported netlist)."""
    nets = {}
    for ref, _lib, _val, _fp, pins, _grp in PARTS:
        for pin, net in pins.items():
            if net is not None:
                nets.setdefault(net, []).append(f"{ref}.{pin}")
    return {n: sorted(v) for n, v in nets.items()}
