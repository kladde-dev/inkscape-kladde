# SPDX-License-Identifier: GPL-2.0-or-later

target_sources(inkscape_base PRIVATE
	backing.cpp
	svg-values.cpp
	ui.cpp

	# -------
	# Headers
	backing.h
	schema.h
	svg-values.h
	ui.h
)
