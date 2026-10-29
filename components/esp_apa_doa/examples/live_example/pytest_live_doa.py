# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut


@pytest.mark.ESP_GMF_COMPONENT
@pytest.mark.esp32s31
def test_live_doa(dut: IdfDut) -> None:
    dut.expect_exact('2-mic DOA running', timeout=120)
