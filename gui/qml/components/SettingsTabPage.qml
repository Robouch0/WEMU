import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import gui 1.0

// A scrolling settings tab. Up and Down walk its controls in order and the view follows focus.
FocusScope {
    id: root

    default property alias content: holder.data
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
        const next = Window.activeFocusItem ? Window.activeFocusItem.nextItemInFocusChain(forward) : null
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
    Keys.onLeftPressed: leaveRequested()
    Keys.onEscapePressed: leaveRequested()

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { root.ensureVisible(root.Window.activeFocusItem) }
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: holder.implicitHeight + 2 * Theme.space5
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Basic.ScrollBar.vertical: Basic.ScrollBar {}

        ColumnLayout {
            id: holder
            x: Theme.space5
            y: Theme.space5
            width: flickable.width - 2 * Theme.space5
            spacing: Theme.space4
        }
    }
}
