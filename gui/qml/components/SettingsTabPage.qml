import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import gui 1.0

// A scrolling settings tab. Up and Down walk its controls in order and the view follows focus.
// Only Escape leaves it, so a stray Left cannot drop the user onto the tab rail.
FocusScope {
    id: root

    default property alias content: holder.data
    property int contentMargin: Theme.space5
    signal leaveRequested()

    function owns(item) {
        for (let current = item; current; current = current.parent)
            if (current === holder) return true
        return false
    }

    function enter() {
        const first = holder.nextItemInFocusChain(true)
        if (owns(first)) first.forceActiveFocus()
    }

    function moveFocus(forward) {
        const current = Window.activeFocusItem
        if (!current) return
        // The chain wraps around, but a tab should stop at its first and last controls.
        if (current === holder.nextItemInFocusChain(!forward)) return
        const next = current.nextItemInFocusChain(forward)
        if (owns(next)) next.forceActiveFocus()
    }

    function ensureVisible(item) {
        if (!item || !owns(item)) return
        const top = item.mapToItem(holder, 0, 0).y + holder.y
        const margin = Theme.space5
        const maxY = Math.max(0, flickable.contentHeight - flickable.height)
        if (top - margin < flickable.contentY)
            flickable.contentY = Math.max(0, top - margin)
        else if (top + item.height + margin > flickable.contentY + flickable.height)
            flickable.contentY = Math.min(maxY, top + item.height + margin - flickable.height)
    }

    Keys.onUpPressed: moveFocus(false)
    Keys.onDownPressed: moveFocus(true)
    Keys.onEscapePressed: leaveRequested()

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { root.ensureVisible(root.Window.activeFocusItem) }
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: holder.implicitHeight + 2 * root.contentMargin
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Basic.ScrollBar.vertical: Basic.ScrollBar {}

        ColumnLayout {
            id: holder
            x: root.contentMargin
            y: root.contentMargin
            width: flickable.width - 2 * root.contentMargin
            spacing: Theme.space4
        }
    }
}
