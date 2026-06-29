package com.websitebeaver.documentscanner.models

/**
 * A lightweight 2D point with double precision coordinates. This replaces
 * org.opencv.core.Point, which was previously only used as a simple (x, y) data
 * holder, so the library no longer needs the OpenCV Java bindings.
 *
 * @param x the horizontal coordinate
 * @param y the vertical coordinate
 */
class Point(@JvmField var x: Double = 0.0, @JvmField var y: Double = 0.0)
