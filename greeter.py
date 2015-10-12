#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#  main_window.py
#
#  Copyright © 2015 Antergos
#
#  This file is part of lightdm-webkit2-greeter, (Greeter).
#
#  Greeter is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
#
#  Greeter is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  The following additional terms are in effect as per Section 7 of the license:
#
#  The preservation of all legal notices and author attributions in
#  the material or in the Appropriate Legal Notices displayed
#  by works containing it is required.
#
#  You should have received a copy of the GNU General Public License
#  along with Greeter; If not, see <http://www.gnu.org/licenses/>.

import sys
from greeter.main_window import GreeterMainView
from PyQt5 import QtWidgets, QtCore
import gettext
import locale


if __name__ == '__main__':
    # setup_gettext()
    app = QtWidgets.QApplication(sys.argv)
    # app.setApplicationName('Antergos Package Assistant')
    # app.setApplicationVersion(POODLE_VERSION)
    screen = app.desktop().availableGeometry(-1)
    greeter_app = GreeterMainView(screen=screen)
    greeter_app.setAttribute(QtCore.Qt.WA_DeleteOnClose)
    # greeter_app.setWindowFlags(QtCore.Qt.BypassWindowManagerHint)

    # app.aboutToQuit.connect(poodle_app.cleanup)

    greeter_app.show()
    sys.exit(app.exec_())