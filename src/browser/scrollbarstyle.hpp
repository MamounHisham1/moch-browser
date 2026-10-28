#pragma once

#include <QString>

class QWebEnginePage;

// Restyles the scrollbars of web content.
//
// Chromium paints its own native scrollbars inside the page and ignores Qt
// style sheets there, so the only way to change them is a user stylesheet
// injected into every document. A QWebEngineScript installed on the page's
// script collection does that: it adds one <style> element as soon as the
// document has a <head>, which survives navigations within the renderer.
namespace ScrollbarStyle
{
// Stylesheet for web content, tinted for the given Moch theme.
QString css(const QString &theme);

// Installs (or replaces) the stylesheet on page and applies it to the
// document already loaded there, so a theme switch is immediate.
void install(QWebEnginePage *page, const QString &theme);
} // namespace ScrollbarStyle
