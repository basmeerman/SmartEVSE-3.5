"""Chint DDSU666 1-phase energy meter profile.

EMConfig[] index 20 (EM_CHINT_1P). FLOAT32, FC3.

Slot note: upstream (dingo35) placed this meter at index 17. This fork has used
17/18 for the Orno meters since Plan 05 and the meter type is persisted in NVS,
so reusing 17 would silently reinterpret an existing Orno configuration as a
Chint one. The fork therefore appends at 20. See docs/upstream-differences.md.
"""

METER_TYPE = 20
METER_NAME = "Chint DDSU666 (1-phase)"
ENDIANNESS = "HBF_HWF"
FUNCTION_CODE = 3
DATA_TYPE = "FLOAT32"

# From EMConfig[20]: URegister=0x2000, UDivisor=1
#                    IRegister=0x2002, IDivisor=0   (A)
#                    PRegister=0x2004, PDivisor=-3  (kW -> W)
#                    ERegister=0x4000, EDivisor=0, ERegister_Exp=0x400A (kWh)
REGISTERS = {
    'current_l1':    0x2002,
    'power_total':   0x2004,
    'energy_import': 0x4000,
    'energy_export': 0x400A,
}

I_DIVISOR = 0
P_DIVISOR = -3
E_DIVISOR = 0
E_DIVISOR_EXP = 0

IS_3PHASE = False

TEST_VECTORS = [
    {
        'name': 'nominal_1phase_16A',
        'description': '1-phase 16A import',
        'current_values': [16.0],
        'power_value': 3.68,
        'energy_import_value': 567.89,
        'energy_export_value': 0.0,
        'expected_current_mA': [16000, 0, 0],
        'expected_power_W': 3680,
        'expected_energy_import_Wh': 567890,
        'expected_energy_export_Wh': 0,
    },
    {
        'name': 'minimum_6A',
        'description': '1-phase 6A import',
        'current_values': [6.0],
        'power_value': 1.38,
        'energy_import_value': 100.0,
        'energy_export_value': 0.0,
        'expected_current_mA': [6000, 0, 0],
        'expected_power_W': 1380,
        'expected_energy_import_Wh': 100000,
        'expected_energy_export_Wh': 0,
    },
    {
        'name': 'export_negative_power',
        'description': '1-phase 10A export - negative power must flip the current sign',
        'current_values': [10.0],
        'power_value': -2.3,
        'energy_import_value': 100.0,
        'energy_export_value': 25.5,
        'expected_current_mA': [-10000, 0, 0],
        'expected_power_W': -2300,
        'expected_energy_import_Wh': 100000,
        'expected_energy_export_Wh': 25500,
    },
    {
        'name': 'zero',
        'description': 'No load',
        'current_values': [0.0],
        'power_value': 0.0,
        'energy_import_value': 0.0,
        'energy_export_value': 0.0,
        'expected_current_mA': [0, 0, 0],
        'expected_power_W': 0,
        'expected_energy_import_Wh': 0,
        'expected_energy_export_Wh': 0,
    },
]
