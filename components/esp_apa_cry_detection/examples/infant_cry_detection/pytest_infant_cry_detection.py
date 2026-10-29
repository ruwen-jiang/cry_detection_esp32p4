# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.esp32s3
@pytest.mark.esp32p4
@idf_parametrize('target', ['esp32s3', 'esp32p4'], indirect=['target'])
def test_infant_cry_detection(dut: IdfDut) -> None:
    dut.expect_exact('Cry detection running', timeout=120)
