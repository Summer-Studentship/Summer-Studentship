import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "Tsunami Barrier Studentship"

    readonly property var sections: [
        { id: "data", title: "Data", description: "Case configuration, dataset manifest and input provenance placeholders.", owners: "SWE-DAT-CFG, SWE-DAT-MAN, SWE-GUI-CAS" },
        { id: "domain", title: "Domain", description: "Regional corridor, local domain, terrain and geometry configuration placeholders.", owners: "SWE-GEO-*, SWE-GUI-CAS" },
        { id: "mesh", title: "Mesh", description: "Mesh preparation, inspection, region and boundary tag placeholders.", owners: "SWE-GEO-MSH, SWE-FVM-MSH, SWE-GUI-VIS" },
        { id: "physics", title: "Physics", description: "Model and physical-parameter configuration placeholders.", owners: "SWE-R2D-*, SWE-L3D-*, SWE-GUI-CAS" },
        { id: "solver", title: "Solver", description: "Numerical-method and execution configuration placeholders.", owners: "SWE-R2D-*, SWE-L3D-*, SWE-GUI-RUN" },
        { id: "run", title: "Run", description: "Validation, preparation, launch, cancellation and status monitoring placeholders.", owners: "SWE-ARC-SVC, SWE-GUI-RUN, SWE-GUI-LOG" },
        { id: "post_processing", title: "Post-processing", description: "Result discovery, metrics, comparisons and visualisation handoff placeholders.", owners: "SWE-CPL-MET, SWE-CPL-CMP, SWE-GUI-POST, SWE-GUI-VIS" }
    ]
    property int activeNavigationIndex: 0
    readonly property bool shellReady: true
    readonly property int navigationCount: sections.length
    readonly property string activeSectionId: sections[activeNavigationIndex].id
    readonly property string activeSectionTitle: sections[activeNavigationIndex].title
    readonly property bool diagnosticPanelVisible: diagnosticPanel.visible

    function activateSection(index) {
        if (index < 0 || index >= sections.length) {
            activeNavigationIndex = 0
            return
        }
        activeNavigationIndex = index
    }

    header: ApplicationHeader {
        sectionTitle: root.activeSectionTitle
        backend: shellController.serviceBackend
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavigationRail {
                Layout.preferredWidth: 232
                Layout.fillHeight: true
                sections: root.sections
                activeIndex: root.activeNavigationIndex
                onSectionRequested: function(index) { root.activateSection(index) }
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 18

                StackLayout {
                    anchors.fill: parent
                    currentIndex: root.activeNavigationIndex
                    DataPage {}
                    DomainPage {}
                    MeshPage {}
                    PhysicsPage {}
                    SolverPage {}
                    RunPage {}
                    PostProcessingPage {}
                }
            }
        }

        DiagnosticPanel {
            id: diagnosticPanel
            Layout.fillWidth: true
            active: shellController.diagnosticActive
            severity: shellController.diagnosticSeverity
            category: shellController.diagnosticCategory
            code: shellController.diagnosticCode
            message: shellController.diagnosticMessage
            operation: shellController.diagnosticOperation
            ruleId: shellController.diagnosticRuleId
            stateChanged: shellController.diagnosticStateChanged
        }

        ServiceStatusBar {
            Layout.fillWidth: true
            backend: shellController.serviceBackend
            boundaryAvailable: shellController.serviceBoundaryAvailable
            solverAvailable: shellController.solverAvailable
            validationAvailable: shellController.validationAvailable
            preparationAvailable: shellController.preparationAvailable
            launchAvailable: shellController.launchAvailable
            cancellationAvailable: shellController.cancellationAvailable
            resultDiscoveryAvailable: shellController.resultDiscoveryAvailable
        }
    }
}
