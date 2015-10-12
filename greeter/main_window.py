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


from PyQt5 import QtWidgets, QtGui, QtWebChannel, QtCore
from PyQt5 import QtWebEngineWidgets as QtWebEng
import gi
gi.require_version('LightDM', '1')
from gi.repository import LightDM
import greeter.resources.compiled_resources as resources_rc

THEME = 'file:///usr/share/lightdm-webkit/themes/antergos/index.html'


class GreeterMainView(QtWidgets.QMainWindow):
    def __init__(self, screen=None, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.greeter = LDMGreeter()
        self.screen = screen
        self.web_page = QtWebEng.QWebEnginePage(self)
        self.web_view = QtWebEng.QWebEngineView(self)
        self.web_page_settings = self.web_page.settings()
        self.global_settings = self.web_page_settings.globalSettings()
        self.channel = QtWebChannel.QWebChannel(self.web_page)

        self.init_view()

        self.web_view.show()

        self.showFullScreen()
        self.activateWindow()

    def init_view(self):
        # self.setFixedSize(966, 605)

        self.web_page.setView(self.web_view)

        self.global_settings.setAttribute(QtWebEng.QWebEngineSettings.LocalContentCanAccessRemoteUrls, True)
        self.global_settings.setAttribute(QtWebEng.QWebEngineSettings.LocalContentCanAccessFileUrls, True)
        self.global_settings.setAttribute(QtWebEng.QWebEngineSettings.ScrollAnimatorEnabled, True)

        # self.init_bridge_channel()
        self.setGeometry(self.screen)
        self.web_view.setGeometry(self.screen)
        self.setCentralWidget(self.web_view)
        self.setContentsMargins(0, 0, 0, 0)
        self.web_page.load(QtCore.QUrl(THEME))

    def init_bridge_channel(self):
        self.web_page.setWebChannel(self.channel)
        self.channel.registerObject('GreeterBridge', self.bridge)


class LDMGreeter(LightDM.GreeterClass):
    def __init__(self):
        super().__init__()
        self.lightdm = LightDM.Greeter()
        self.lightdm.connect_sync()
