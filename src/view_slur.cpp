/////////////////////////////////////////////////////////////////////////////
// Name:        view_slur.cpp
// Author:      Laurent Pugin
// Created:     2018
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "view.h"

//----------------------------------------------------------------------------

#include <assert.h>
#include <iostream>
#include <sstream>

//----------------------------------------------------------------------------

#include "bboxdevicecontext.h"
#include "comparison.h"
#include "devicecontext.h"
#include "doc.h"
#include "ftrem.h"
#include "functorparams.h"
#include "layer.h"
#include "layerelement.h"
#include "measure.h"
#include "note.h"
#include "options.h"
#include "slur.h"
#include "staff.h"
#include "system.h"
#include "timeinterface.h"
#include "vrv.h"

namespace vrv {

//----------------------------------------------------------------------------
// View - Slur
//----------------------------------------------------------------------------

void View::DrawSlur(DeviceContext *dc, Slur *slur, int x1, int x2, Staff *staff, char spanningType, Object *graphic)
{
    assert(dc);
    assert(slur);
    assert(staff);

    FloatingPositioner *positioner = slur->GetCurrentFloatingPositioner();
    assert(positioner && positioner->Is(FLOATING_CURVE_POSITIONER));
    FloatingCurvePositioner *curve = vrv_cast<FloatingCurvePositioner *>(positioner);
    assert(curve);

    if (m_initializeSlurs && dc->Is(BBOX_DEVICE_CONTEXT)
        && (curve->GetDir() == curvature_CURVEDIR_NONE || curve->IsCrossStaff())) {
        this->DrawSlurInitial(curve, slur, x1, x2, staff, spanningType);
    }

    Point points[4];
    curve->GetPoints(points);

    if (graphic)
        dc->ResumeGraphic(graphic, graphic->GetUuid());
    else
        dc->StartGraphic(slur, "", slur->GetUuid(), false);

    int penStyle = AxSOLID;
    switch (slur->GetLform()) {
        case LINEFORM_dashed: penStyle = AxSHORT_DASH; break;
        case LINEFORM_dotted: penStyle = AxDOT; break;
        case LINEFORM_wavy:
        // TODO: Implement wavy slur.
        default: break;
    }
    const int penWidth
        = m_doc->GetOptions()->m_slurEndpointThickness.GetValue() * m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
    if (m_slurThicknessCoefficient <= 0) {
        m_slurThicknessCoefficient
            = BoundingBox::GetBezierThicknessCoefficient(points, curve->GetThickness(), curve->GetAngle(), penWidth);
    }
    DrawThickBezierCurve(dc, points, m_slurThicknessCoefficient * curve->GetThickness(), staff->m_drawingStaffSize,
        penWidth, curve->GetAngle(), penStyle);

    /*
    int i;
    for (i = 0; i <= 10; ++i) {
        Point p = BoundingBox::CalcDeCasteljau(points, (double)i / 10.0);
        DrawDot(dc, p.x, p.y, staff->m_drawingStaffSize);
    }
    */

    if (graphic)
        dc->EndResumedGraphic(graphic, this);
    else
        dc->EndGraphic(slur, this);
}

void View::DrawSlurInitial(FloatingCurvePositioner *curve, Slur *slur, int x1, int x2, Staff *staff, char spanningType)
{
    /************** parent layers **************/

    LayerElement *start = slur->GetStart();
    LayerElement *end = slur->GetEnd();

    if (!start || !end) {
        // no start and end, obviously nothing to do...
        return;
    }

    StemmedDrawingInterface *startStemDrawInterface = dynamic_cast<StemmedDrawingInterface *>(start);
    StemmedDrawingInterface *endStemDrawInterface = dynamic_cast<StemmedDrawingInterface *>(end);

    data_STEMDIRECTION startStemDir = STEMDIRECTION_NONE;
    if (startStemDrawInterface) {
        startStemDir = startStemDrawInterface->GetDrawingStemDir();
    }
    data_STEMDIRECTION endStemDir = STEMDIRECTION_NONE;
    if (endStemDrawInterface) {
        endStemDir = endStemDrawInterface->GetDrawingStemDir();
    }

    Layer *layer = NULL;
    LayerElement *layerElement = NULL;
    // For now, with timestamps, get the first layer. We should eventually look at the @layerident (not implemented)
    if (!start->Is(TIMESTAMP_ATTR)) {
        layer = dynamic_cast<Layer *>(start->GetFirstAncestor(LAYER));
        layerElement = start;
    }
    else if (!end->Is(TIMESTAMP_ATTR)) {
        layer = dynamic_cast<Layer *>(end->GetFirstAncestor(LAYER));
        layerElement = end;
    }
    if (layerElement && layerElement->m_crossStaff) layer = layerElement->m_crossLayer;

    // At this stage layer can still be NULL for slurs with @tstamp and @tstamp2

    if (start->m_crossStaff != end->m_crossStaff) {
        curve->SetCrossStaff(end->m_crossStaff);
    }
    // Check if the two elements are in different staves (but themselves not cross-staff)
    else {
        Staff *startStaff = vrv_cast<Staff *>(start->GetFirstAncestor(STAFF));
        Staff *endStaff = vrv_cast<Staff *>(end->GetFirstAncestor(STAFF));
        if (startStaff && endStaff && (startStaff->GetN() != endStaff->GetN())) curve->SetCrossStaff(endStaff);
    }

    if (!start->Is(TIMESTAMP_ATTR) && !end->Is(TIMESTAMP_ATTR) && (spanningType == SPANNING_START_END)) {
        System *system = vrv_cast<System *>(staff->GetFirstAncestor(SYSTEM));
        assert(system);
        // If we have a start to end situation, then store the curvedir in the slur for mixed drawing stem dir
        // situations
        if (system->HasMixedDrawingStemDir(start, end)) {
            if (!curve->IsCrossStaff()) {
                slur->SetDrawingCurvedir(curvature_CURVEDIR_above);
            }
            else {
                curvature_CURVEDIR curveDir = system->GetPreferredCurveDirection(start, end, slur);
                slur->SetDrawingCurvedir(curveDir != curvature_CURVEDIR_NONE ? curveDir : curvature_CURVEDIR_above);
            }
        }
    }

    /************** note stem dir **************/

    data_STEMDIRECTION stemDir = STEMDIRECTION_NONE;
    if (spanningType == SPANNING_START_END) {
        stemDir = startStemDir;
    }
    // This is the case when the slur is split over two system of two pages.
    // In this case, we are now drawing its beginning to the end of the measure (i.e., the last aligner)
    else if (spanningType == SPANNING_START) {
        stemDir = startStemDir;
    }
    // Now this is the case when the slur is split but we are drawing the end of it
    else if (spanningType == SPANNING_END) {
        stemDir = endStemDir;
    }
    // Finally, slur accross an entire system; use the staff position and up (see below)
    else {
        stemDir = STEMDIRECTION_down;
    }

    /************** direction **************/

    const int center = staff->GetDrawingY() - m_doc->GetDrawingStaffSize(staff->m_drawingStaffSize) / 2;
    const bool isAboveStaffCenter = start->GetDrawingY() > center;
    curvature_CURVEDIR drawingCurveDir
        = slur->GetPreferredCurveDirection(m_doc, layer, layerElement, stemDir, isAboveStaffCenter);

    /************** adjusting y position **************/

    int y1 = staff->GetDrawingY();
    int y2 = staff->GetDrawingY();
    std::pair<Point, Point> adjustedPoints = slur->AdjustCoordinates(
        m_doc, staff, std::make_pair(Point(x1, y1), Point(x2, y2)), spanningType, drawingCurveDir);

    /************** y position **************/

    if (drawingCurveDir == curvature_CURVEDIR_above) {
        adjustedPoints.first.y += 1.25 * m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
        adjustedPoints.second.y += 1.25 * m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
    }
    else {
        adjustedPoints.first.y -= 1.25 * m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
        adjustedPoints.second.y -= 1.25 * m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
    }

    Point points[4];
    points[0] = adjustedPoints.first;
    points[3] = adjustedPoints.second;

    float angle = CalcInitialSlur(curve, slur, staff, drawingCurveDir, points);
    int thickness = m_doc->GetDrawingUnit(staff->m_drawingStaffSize) * m_options->m_slurMidpointThickness.GetValue();

    curve->UpdateCurveParams(points, angle, thickness, drawingCurveDir);

    /************** articulation **************/

    // First get all artic children
    ClassIdComparison matchType(ARTIC);
    ListOfObjects artics;

    // the normal case or start
    if ((spanningType == SPANNING_START_END) || (spanningType == SPANNING_START)) {
        start->FindAllDescendantByComparison(&artics, &matchType);
        // Then the @n of each first staffDef
        for (auto &object : artics) {
            Artic *artic = vrv_cast<Artic *>(object);
            assert(artic);
            if (artic->IsOutsideArtic()) {
                if ((artic->GetPlace() == STAFFREL_above) && (drawingCurveDir == curvature_CURVEDIR_above)) {
                    artic->AddSlurPositioner(curve, true);
                }
                else if ((artic->GetPlace() == STAFFREL_below) && (drawingCurveDir == curvature_CURVEDIR_below)) {
                    artic->AddSlurPositioner(curve, true);
                }
            }
        }
    }
    // normal case or end
    if ((spanningType == SPANNING_START_END) || (spanningType == SPANNING_END)) {
        end->FindAllDescendantByComparison(&artics, &matchType);
        // Then the @n of each first staffDef
        for (auto &object : artics) {
            Artic *artic = vrv_cast<Artic *>(object);
            assert(artic);
            if (artic->IsOutsideArtic()) {
                if ((artic->GetPlace() == STAFFREL_above) && (drawingCurveDir == curvature_CURVEDIR_above)) {
                    artic->AddSlurPositioner(curve, false);
                }
                else if ((artic->GetPlace() == STAFFREL_below) && (drawingCurveDir == curvature_CURVEDIR_below)) {
                    artic->AddSlurPositioner(curve, false);
                }
            }
        }
    }

    return;
}

float View::CalcInitialSlur(
    FloatingCurvePositioner *curve, Slur *slur, Staff *staff, curvature_CURVEDIR curveDir, Point points[4])
{
    // For now we pick C1 = P1 and C2 = P2
    BezierCurve bezier(points[0], points[0], points[3], points[3]);

    /************** content **************/

    const std::vector<LayerElement *> elements = slur->CollectSpannedElements(staff, bezier.p1.x, bezier.p2.x);

    Staff *startStaff = slur->GetStart()->m_crossStaff ? slur->GetStart()->m_crossStaff
                                                       : vrv_cast<Staff *>(slur->GetStart()->GetFirstAncestor(STAFF));
    Staff *endStaff = slur->GetEnd()->m_crossStaff ? slur->GetEnd()->m_crossStaff
                                                   : vrv_cast<Staff *>(slur->GetEnd()->GetFirstAncestor(STAFF));
    curve->ClearSpannedElements();
    for (auto element : elements) {

        Point pRotated;
        Point pLeft;
        pLeft.x = element->GetSelfLeft();
        Point pRight;
        pRight.x = element->GetSelfRight();
        if (((pLeft.x > bezier.p1.x) && (pLeft.x < bezier.p2.x))
            || ((pRight.x > bezier.p1.x) && (pRight.x < bezier.p2.x))) {
            CurveSpannedElement *spannedElement = new CurveSpannedElement();
            spannedElement->m_boundingBox = element;
            curve->AddSpannedElement(spannedElement);
        }

        if (!curve->IsCrossStaff() && element->m_crossStaff) {
            curve->SetCrossStaff(element->m_crossStaff);
        }
    }

    // Ties can be broken across systems, so we have to look for all floating curve positioners that represent them.
    // This might be refined later, since using the entire bounding box of a tie for collision avoidance with slurs is
    // coarse.
    ArrayOfFloatingPositioners tiePositioners = staff->GetAlignment()->FindAllFloatingPositioners(TIE);
    if (startStaff && (startStaff != staff) && startStaff->GetAlignment()) {
        const ArrayOfFloatingPositioners startTiePositioners
            = startStaff->GetAlignment()->FindAllFloatingPositioners(TIE);
        std::copy(startTiePositioners.begin(), startTiePositioners.end(), std::back_inserter(tiePositioners));
    }
    else if (endStaff && (endStaff != staff) && endStaff->GetAlignment()) {
        const ArrayOfFloatingPositioners endTiePositioners = endStaff->GetAlignment()->FindAllFloatingPositioners(TIE);
        std::copy(endTiePositioners.begin(), endTiePositioners.end(), std::back_inserter(tiePositioners));
    }
    for (FloatingPositioner *positioner : tiePositioners) {
        if (positioner->GetAlignment()->GetParentSystem() == curve->GetAlignment()->GetParentSystem()) {
            if (positioner->HasContentBB() && (positioner->GetContentRight() > bezier.p1.x)
                && (positioner->GetContentLeft() < bezier.p2.x)) {
                CurveSpannedElement *spannedElement = new CurveSpannedElement();
                spannedElement->m_boundingBox = positioner;
                curve->AddSpannedElement(spannedElement);
            }
        }
    }

    /************** angle **************/

    bool dontAdjustAngle = curve->IsCrossStaff() || slur->GetStart()->IsGraceNote();
    // If slur is cross-staff (where we don't want to adjust angle) but x distance is too small - adjust angle anyway
    if ((bezier.p2.x - bezier.p1.x) != 0 && curve->IsCrossStaff()) {
        dontAdjustAngle = std::abs((bezier.p2.y - bezier.p1.y) / (bezier.p2.x - bezier.p1.x)) < 4;
    }

    const float nonAdjustedAngle
        = (bezier.p2 == bezier.p1) ? 0 : atan2(bezier.p2.y - bezier.p1.y, bezier.p2.x - bezier.p1.x);
    const float slurAngle
        = dontAdjustAngle ? nonAdjustedAngle : slur->GetAdjustedSlurAngle(m_doc, bezier.p1, bezier.p2, curveDir);
    bezier.p2 = BoundingBox::CalcPositionAfterRotation(bezier.p2, -slurAngle, bezier.p1);

    /************** control points **************/

    bezier.CalcInitialControlPointParams(m_doc, slurAngle, staff->m_drawingStaffSize);
    bezier.UpdateControlPoints(curveDir);
    bezier.Rotate(slurAngle, bezier.p1);

    points[0] = bezier.p1;
    points[1] = bezier.c1;
    points[2] = bezier.c2;
    points[3] = bezier.p2;

    return slurAngle;
}

} // namespace vrv
