import QtQuick

Item {
    id: root

    property color stripeColor: "white"
    property real stripeWidth: 10
    property real stripeGap: 10
    property int animationDuration: 600

    implicitHeight: 6
    clip: true

    property real _offset: 0
    NumberAnimation on _offset {
        from: 1; to: 0
        duration: root.animationDuration
        loops: Animation.Infinite
        running: root.visible
    }

    Canvas {
        id: canvas
        width: parent.width + root.stripeWidth + root.stripeGap
        height: parent.height
        x: -root._offset * (root.stripeWidth + root.stripeGap)

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var step = root.stripeWidth + root.stripeGap
            var h = height
            ctx.fillStyle = root.stripeColor
            for (var i = -h; i < width + h; i += step) {
                ctx.beginPath()
                ctx.moveTo(i, h)
                ctx.lineTo(i + root.stripeWidth, h)
                ctx.lineTo(i + root.stripeWidth + h, 0)
                ctx.lineTo(i + h, 0)
                ctx.closePath()
                ctx.fill()
            }
        }

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()

        Connections {
            target: root
            function onStripeColorChanged() { canvas.requestPaint() }
            function onStripeWidthChanged() { canvas.requestPaint() }
            function onStripeGapChanged() { canvas.requestPaint() }
        }
    }
}
