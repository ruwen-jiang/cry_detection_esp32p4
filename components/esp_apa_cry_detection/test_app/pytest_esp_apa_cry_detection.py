# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize

CASE_TIMEOUT_S = 1800


@pytest.mark.esp32s3
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_esp_apa_cry_detection_esp32s3(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=CASE_TIMEOUT_S, reset=True)


@pytest.mark.esp32p4
@idf_parametrize('target', ['esp32p4'], indirect=['target'])
def test_esp_apa_cry_detection_esp32p4(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=CASE_TIMEOUT_S, reset=True)


@pytest.mark.esp32s31
@idf_parametrize('target', ['esp32s31'], indirect=['target'])
def test_esp_apa_cry_detection_esp32s31(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=CASE_TIMEOUT_S, reset=True)
