/*
 * Copyright (c) 2011 Damien Grauser
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "HrPwPlot.h"
#include "MainWindow.h"
#include "HrPwWindow.h"
#include "RideFile.h"
#include "RideItem.h"
#include "Zones.h"
#include "Settings.h"
#include "Colors.h"

#include <assert.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_marker.h>
#include <qwt_text.h>
#include <qwt_symbol.h>
#include <qwt_legend.h>
#include <qwt_data.h>


static inline double
max(double a, double b) { if (a > b) return a; else return b; }

HrPwPlot::HrPwPlot(MainWindow *mainWindow, HrPwWindow *hrPwWindow) :
    QwtPlot(hrPwWindow),
    hrPwWindow(hrPwWindow),
    mainWindow(mainWindow),
    bg(NULL), delay(-1),
    minHr(50), minWatt(50), maxWatt(500),
    settings(GC_SETTINGS_CO, GC_SETTINGS_APP),
    unit(settings.value(GC_UNIT))
{
    setCanvasBackground(Qt::white);
    canvas()->setFrameStyle(QFrame::NoFrame);
    setXTitle(); // Power (Watts)

    // Linear Regression Curve
    regCurve = new QwtPlotCurve("reg");
    regCurve->setPen(QPen(GColor(CPLOTMARKER)));
    regCurve->attach(this);

    // Power distribution
    wattsStepCurve = new QwtPlotCurve("Power");
    wattsStepCurve->setStyle(QwtPlotCurve::Steps);
    wattsStepCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    QColor wattsColor = QColor(200,200,255);
    QColor wattsColor2 = QColor(100,100,255);
    wattsStepCurve->setPen(QPen(wattsColor2));
    wattsStepCurve->setBrush(QBrush(wattsColor));

    wattsStepCurve->attach(this);

    // Hr distribution
    hrStepCurve = new QwtPlotCurve("Hr");
    hrStepCurve->setStyle(QwtPlotCurve::Steps);
    hrStepCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    QColor hrColor = QColor(255,200,200);
    QColor hrColor2 = QColor(255,100,100);
    hrStepCurve->setPen(QPen(hrColor2));
    hrStepCurve->setBrush(QBrush(hrColor));
    hrStepCurve->attach(this);

    // Heart Rate Curve

    hrCurves.resize(36);
    for (int i = 0; i < 36; ++i) {
        hrCurves[i] = new QwtPlotCurve;
        hrCurves[i]->attach(this);
    }

    // Grid
    grid = new QwtPlotGrid();
    grid->enableX(false);
    QPen gridPen;
    gridPen.setStyle(Qt::DotLine);
    gridPen.setColor(GColor(CPLOTGRID));
    grid->setPen(gridPen);
    grid->attach(this);


    // axis markers
    r_mrk1 = new QwtPlotMarker;
    r_mrk2 = new QwtPlotMarker;
    r_mrk1->attach(this);
    r_mrk2->attach(this);

    shade_zones = true;
}

struct DataPoint {
    double time, hr, watts;
    int inter;
    DataPoint(double t, double h, double w, int i) :
        time(t), hr(h), watts(w), inter(i) {}
};

void
HrPwPlot::setAxisTitle(int axis, QString label)
{
    // setup the default fonts
    QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
    stGiles.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
    stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

    QwtText title(label);
    title.setFont(stGiles);
    QwtPlot::setAxisFont(axis, stGiles);
    QwtPlot::setAxisTitle(axis, title);
}

void
HrPwPlot::recalc()
{
    if (timeArray.count() == 0)
        return;

    int rideTimeSecs = (int) ceil(timeArray[arrayLength - 1]);
    if (rideTimeSecs > 7*24*60*60) {
        return;
    }




    // Find Hr Delay
    //int delayori = findDelay(wattsArray, hrArray, rideTimeSecs/5);
    //int delay  = 0;


    // ------ smoothing -----
    // ----------------------
    double totalWatts = 0.0;
    double totalHr = 0.0;
    QList<DataPoint*> list;
    int i = 0;
    QVector<double> smoothWatts(rideTimeSecs + 1);
    QVector<double> smoothHr(rideTimeSecs + 1);
    QVector<double> smoothTime(rideTimeSecs + 1);
    int decal=0;

    /*for (int secs = 0; ((secs < smooth) && (secs < rideTimeSecs)); ++secs) {
        smoothWatts[secs] = 0.0;
        smoothHr[secs]    = 0.0;
    }*/

    //int interval = 0;
    int smooth = hrPwWindow->smooth;

    for (int secs = smooth; secs <= rideTimeSecs; ++secs) {
        while ((i < arrayLength) && (timeArray[i] <= secs)) {
            DataPoint *dp =
                new DataPoint(timeArray[i], hrArray[i], wattsArray[i], interArray[i]);
	
            totalWatts += wattsArray[i];
                totalHr    += hrArray[i];
	        list.append(dp);

            ++i;
        }
        while (!list.empty() && (list.front()->time < secs - smooth)) {
            DataPoint *dp = list.front();
            list.removeFirst();
            totalWatts -= dp->watts;
            totalHr    -= dp->hr;
            delete dp;
        }
        if (list.empty()) {
            ++decal;
        }
        else {
            smoothWatts[secs-decal]    = totalWatts / list.size();
            smoothHr[secs-decal]       = totalHr / list.size();
            // Utiliser interval du fichier
            //if (smooth/list.size()>0)
            //	interval = smooth/list.size();
        }
        smoothTime[secs]  = secs / 60.0;
    }

    rideTimeSecs = rideTimeSecs-decal;
    smoothWatts.resize(rideTimeSecs);
    smoothHr.resize(rideTimeSecs);

    // Clip to max
    QVector<double> clipWatts(rideTimeSecs);
    QVector<double> clipHr(rideTimeSecs);

    /*for (int secs = 0; secs < rideTimeSecs; ++secs) {
        clipWatts[secs] = 0.0;
        clipHr[secs] = 0.0;
    }*/

    decal = 0;
    for (int secs = 0; secs < rideTimeSecs; ++secs) {
                if (smoothHr[secs]>= minHr && smoothWatts[secs]>= minWatt && smoothWatts[secs]<maxWatt) {
                        clipWatts[secs-decal]    = smoothWatts[secs];
                        clipHr[secs-decal]    = smoothHr[secs];
                }
                else
                        decal ++;
        }
    rideTimeSecs = rideTimeSecs-decal;
    clipWatts.resize(rideTimeSecs);
    clipHr.resize(rideTimeSecs);



    // Find Hr Delay
    if (delay == -1)
       delay = hrPwWindow->findDelay(clipWatts, clipHr, clipWatts.size());

    // Apply delay
    QVector<double> delayWatts(rideTimeSecs-delay);
    QVector<double> delayHr(rideTimeSecs-delay);

    for (int secs = 0; secs < rideTimeSecs-delay; ++secs) {
        delayWatts[secs]    = clipWatts[secs];
        delayHr[secs]    = clipHr[secs+delay];
    }
    rideTimeSecs = rideTimeSecs-delay;

    double rpente = hrPwWindow->pente(delayWatts, delayHr, delayWatts.size());
    double rordonnee = hrPwWindow->ordonnee(delayWatts, delayHr, delayWatts.size());
    double maxr = hrPwWindow->corr(delayWatts, delayHr, delayWatts.size());

    // ----- limit plotted points ---
    // ----------------------
    int intpoints = 10; // could be ride length dependent
    int nbpoints = (int)floor(rideTimeSecs/intpoints);

    QVector<double> plotedWatts(nbpoints);
    QVector<double> plotedHr(nbpoints);

    for (int secs = 0; secs < nbpoints; ++secs) {
        plotedWatts[secs]    = clipWatts[secs*intpoints];
        plotedHr[secs]    = clipHr[secs*intpoints];
    }
    int nbpoints2 = (int)floor(nbpoints/36)+2;

    double *plotedWattsArray[36];
    double *plotedHrArray[36];

    for (int i = 0; i < 36; ++i) {
        plotedWattsArray[i]= new double[nbpoints2];
        plotedHrArray[i]= new double[nbpoints2];
    }

    for (int secs = 0; secs < nbpoints; ++secs) {
        for (int i = 0; i < 36; ++i) {
            if (secs >= i*nbpoints2 && secs< (i+1)*nbpoints2) {
                plotedWattsArray[i][secs-i*nbpoints2] = plotedWatts[secs-i];
                plotedHrArray[i][secs-i*nbpoints2]    = plotedHr[secs-i];
            }
        }
    }


    for (int i = 0; i < 36; ++i) {

    	if (nbpoints-i*nbpoints2>0) {

                hrCurves[i]->setData(plotedWattsArray[i], plotedHrArray[i], (nbpoints-i*nbpoints2<nbpoints2?nbpoints-i*nbpoints2:nbpoints2));
    		hrCurves[i]->setVisible(true);
    	} else
    		hrCurves[i]->setVisible(false);
    }


    setAxisScale(xBottom, 0.0, maxWatt);

    setYMax();
    refreshZoneLabels();

    QString labelp;

    labelp.setNum(rpente, 'f', 3);
    QString labelo;
    labelo.setNum(rordonnee, 'f', 1);

    QString labelr;
    labelr.setNum(maxr, 'f', 3);
    QString labeldelay;
    labeldelay.setNum(delay);

    int power150 =  (int)floor((150-rordonnee)/rpente);
    QString labelpower150;
    labelpower150.setNum(power150);

    QwtText textr = QwtText(labelp+"*x+"+labelo+" : R "+labelr+" ("+labeldelay+") \n Power@150:"+labelpower150+"W");
    textr.setFont(QFont("Helvetica", 10, QFont::Bold));
    textr.setColor(Qt::black);

    r_mrk1->setValue(0,0);
    r_mrk1->setLineStyle(QwtPlotMarker::VLine);
    r_mrk1->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    r_mrk1->setLinePen(QPen(Qt::black, 0, Qt::DashDotLine));
    double moyennewatt = hrPwWindow->moyenne(clipWatts, clipWatts.size());
    r_mrk1->setValue(moyennewatt, 0.0);
    r_mrk1->setLabel(textr);

    r_mrk2->setValue(0,0);
    r_mrk2->setLineStyle(QwtPlotMarker::HLine);
    r_mrk2->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    r_mrk2->setLinePen(QPen(Qt::black, 0, Qt::DashDotLine));
    double moyennehr = hrPwWindow->moyenne(clipHr,  clipHr.size());
    r_mrk2->setValue(0.0,moyennehr);

    addWattStepCurve(clipWatts, clipWatts.size());
    addHrStepCurve(clipHr, clipHr.size());

    addRegLinCurve(rpente, rordonnee);

    setJoinLine(joinLine);
    replot();
}

void
HrPwPlot::setYMax()
{
    double ymax = 0;
    QString ylabel = "";
    for (int i = 0; i < 36; ++i) {
	    if (hrCurves[i]->isVisible()) {
	        ymax = max(ymax, hrCurves[i]->maxYValue());
	        //ylabel += QString((ylabel == "") ? "" : " / ") + "BPM";
	    }
    }
    setAxisScale(yLeft, minHr, ymax * 1.2);
    setAxisTitle(yLeft, tr("Heart Rate(BPM)"));
}

void
HrPwPlot::addWattStepCurve(QVector<double> &finalWatts, int nbpoints)
{
    QMap<double,double> powerHist;

    for (int h=0; h< nbpoints; ++h) {
        if (powerHist.contains(finalWatts[h]))
            powerHist[finalWatts[h]] += 1;
        else
            powerHist[finalWatts[h]] = 1;
    }
    int maxPower = 500;
    double *array = new double[maxPower];

    for (int i = 0; i < maxPower; ++i)
        array[i] = 0.0;

    QMapIterator<double,double> k(powerHist);
    while (k.hasNext()) {
        k.next();
        array[(int) round(k.key())] += k.value();
    }

    int nbSteps = (int) ceil((maxPower - 1) / 10);
    QVector<double> smoothWattsStep(nbSteps+1);
    QVector<double> smoothTimeStep(nbSteps+1);

    int t;
    for (t = 1; t < nbSteps; ++t) {
    	int low = t * 10;
    	int high = low + 10;

        smoothWattsStep[t] = low;
        smoothTimeStep[t]  = minHr;
        while (low < high) {
        	smoothTimeStep[t] += array[low++]/ nbpoints * 300;
        }
    }
    smoothTimeStep[t] = 0.0;
    smoothWattsStep[t] = t * 10;

    wattsStepCurve->setData(smoothWattsStep.data(), smoothTimeStep.data(), nbSteps+1);
}

void
HrPwPlot::addHrStepCurve(QVector<double> &finalHr, int nbpoints)
{
    QMap<double,double> hrHist;
    for (int h=0; h< nbpoints; ++h) {
            if (hrHist.contains(finalHr[h]))
                hrHist[finalHr[h]] += 1;
            else
                hrHist[finalHr[h]] = 1;
    }
    int maxHr = 220;

    double *array = new double[maxHr];
    for (int i = 0; i < maxHr; ++i)
        array[i] = 0.0;
    QMapIterator<double,double> l(hrHist);
    while (l.hasNext()) {
        l.next();
        array[(int) round(l.key())] += l.value();
    }


    int nbSteps = (int) ceil((maxHr - 1) / 2);
    QVector<double> smoothHrStep(nbSteps+1);
    QVector<double> smoothTimeStep2(nbSteps+1);

    int t;
    for (t = 1; t < nbSteps; ++t) {
        int low = t * 2;
        int high = low + 2;

        smoothHrStep[t] = low;
        smoothTimeStep2[t]  = 0.0;
        while (low < high) {
        	smoothTimeStep2[t] += array[low++]/ nbpoints * 500;
        }
    }
    smoothTimeStep2[t] = 0.0;
    smoothHrStep[t] = t * 2;

    hrStepCurve->setData(smoothTimeStep2.data(), smoothHrStep.data(), nbSteps+1);
}

void
HrPwPlot::addRegLinCurve( double rpente, double rordonnee)
{
    double regWatts[]     = {0, 0};
    double regHr[]        = {0, 500};

    regWatts[0] = regHr[0]*rpente+rordonnee;
    regWatts[1] = regHr[1]*rpente+rordonnee;

    regCurve->setData(regHr, regWatts, 2);
}

void
HrPwPlot::setXTitle()
{
    setAxisTitle(xBottom, tr("Power (Watts)"));
}

void
HrPwPlot::setDataFromRide(RideItem *_rideItem)
{
    rideItem = _rideItem;

    // ignore null / bad rides
    if (!_rideItem || !_rideItem->ride()) return;

    RideFile *ride = rideItem->ride();

    const RideFileDataPresent *dataPresent = ride->areDataPresent();
    int npoints = ride->dataPoints().size();

    if (dataPresent->watts && dataPresent->hr) {
        wattsArray.resize(npoints);
        hrArray.resize(npoints);
        timeArray.resize(npoints);
        interArray.resize(npoints);

        arrayLength = 0;
        //QListIterator<RideFilePoint*> i(ride->dataPoints());
        //while (i.hasNext()) {
        foreach (const RideFilePoint *point, ride->dataPoints()) {
            //RideFilePoint *point = i.next();
            if (!timeArray.empty())
                timeArray[arrayLength]  = point->secs;
            if (!wattsArray.empty())
                wattsArray[arrayLength] = max(0, point->watts);
            if (!hrArray.empty())
                hrArray[arrayLength]    = max(0, point->hr);
            if (!interArray.empty())
                interArray[arrayLength] = point->interval;
            ++arrayLength;
        }

        delay = -1;
        recalc();
    }
}

void
HrPwPlot::setJoinLine(bool value)
{

    joinLine = value;

    for (int i = 0; i < 36; ++i) {
        QColor color = QColor(255,255,255);
        color.setHsv(60+i*(360/36), 255,255,255);
        if (value) {
            QwtSymbol sym;
            sym.setStyle(QwtSymbol::NoSymbol);

            QPen pen = QPen(color);
            pen.setWidth(1);
            hrCurves[i]->setPen(pen);
            hrCurves[i]->setStyle(QwtPlotCurve::Lines);
            hrCurves[i]->setSymbol(sym);
        } else {
            QwtSymbol sym;
            sym.setStyle(QwtSymbol::Ellipse);
            sym.setSize(5);
            sym.setPen(QPen(color));
            sym.setBrush(QBrush(color));
            hrCurves[i]->setPen(Qt::NoPen);
            hrCurves[i]->setStyle(QwtPlotCurve::Dots);
            hrCurves[i]->setSymbol(sym);
        }
        //hrCurves[i].setRenderHint(QwtPlotItem::RenderAntialiased);
    }

}

void
HrPwPlot::pointHover(QwtPlotCurve *curve, int index)
{
    if (index >= 0) {

        double yvalue = curve->y(index);
        double xvalue = curve->x(index);

        // output the tooltip
        QString text = QString("%1 %2\n%3 %4")
                        .arg(yvalue, 0, 'f', 0)
                        .arg(this->axisTitle(curve->yAxis()).text())
                        .arg(xvalue, 0, 'f', 2)
                        .arg(this->axisTitle(curve->xAxis()).text());

        // set that text up
        tooltip->setText(text);
    } else {
        tooltip->setText("");
    }
}

/*----------------------------------------------------------------------
 * Draw Power Zone Shading on Background (here to end of source file)
 *--------------------------------------------------------------------*/
class HrPwPlotBackground: public QwtPlotItem
{
    private:
        HrPwPlot *parent;

    public:
        HrPwPlotBackground(HrPwPlot *_parent) {
            setZ(0.0);
            parent = _parent;
        }

        virtual int rtti() const {
            return QwtPlotItem::Rtti_PlotUserItem;
        }

        virtual void draw(QPainter *painter,
              const QwtScaleMap &xMap, const QwtScaleMap &,
              const QRect &rect) const {

            RideItem *rideItem = parent->rideItem;

            if (! rideItem)
                return;

            const Zones *zones = rideItem->zones;
            int zone_range     = rideItem->zoneRange();

            if (parent->isShadeZones() && zones && (zone_range >= 0)) {
                QList <int> zone_lows = zones->getZoneLows(zone_range);
                int num_zones = zone_lows.size();
                if (num_zones > 0) {
                    for (int z = 0; z < num_zones; z ++) {
                        QRect r = rect;

                        QColor shading_color = zoneColor(z, num_zones);
                        shading_color.setHsv(
                            shading_color.hue(),
                            shading_color.saturation() / 4,
                            shading_color.value()
                        );
                        r.setLeft(xMap.transform(zone_lows[z]));
                        if (z + 1 < num_zones)
                            r.setRight(xMap.transform(zone_lows[z + 1]));
                        if (r.left() <= r.right())
                            painter->fillRect(r, shading_color);
                    }
                }
            }
    }
};

// Zone labels are drawn if power zone bands are enabled, automatically
// at the center of the plot
class HrPwPlotZoneLabel: public QwtPlotItem
{
    private:
        HrPwPlot *parent;
        int zone_number;
        double watts;
        QwtText text;

    public:
        HrPwPlotZoneLabel(HrPwPlot *_parent, int _zone_number) {
            parent = _parent;
            zone_number = _zone_number;

            RideItem *rideItem = parent->rideItem;


            if (! rideItem)
                return;

            const Zones *zones = rideItem->zones;
            int zone_range     = rideItem->zoneRange();

            // create new zone labels if we're shading
            if (parent->isShadeZones() && zones && (zone_range >= 0)) {
                QList <int> zone_lows = zones->getZoneLows(zone_range);
                QList <QString> zone_names = zones->getZoneNames(zone_range);
                int num_zones = zone_lows.size();
                assert(zone_names.size() == num_zones);
                if (zone_number < num_zones) {
                    watts =
                        (
                        (zone_number + 1 < num_zones) ?
                         0.5 * (zone_lows[zone_number] + zone_lows[zone_number + 1]) :
                        (
                        (zone_number > 0) ?
                        (1.5 * zone_lows[zone_number] - 0.5 * zone_lows[zone_number - 1]) :
                        2.0 * zone_lows[zone_number]
                        )
                        );

                    text = QwtText(zone_names[zone_number]);
                    text.setFont(QFont("Helvetica",24, QFont::Bold));
                    QColor text_color = zoneColor(zone_number, num_zones);
                    text_color.setAlpha(64);
                    text.setColor(text_color);
                }
            }

            setZ(1.0 + zone_number / 100.0);
        }

        virtual int rtti() const {
            return QwtPlotItem::Rtti_PlotUserItem;
        }

        void draw(QPainter *painter,
                const QwtScaleMap &xMap, const QwtScaleMap &,
                const QRect &rect) const {
            if (parent->isShadeZones()) {
                int y = (rect.bottom() + rect.top()) / 2;
                int x = xMap.transform(watts);

                // the following code based on source for QwtPlotMarker::draw()
                QRect tr(QPoint(0, 0), text.textSize(painter->font()));
                tr.moveCenter(QPoint(x, y));
                text.draw(painter, tr);
            }
        }
};

int
HrPwPlot::isShadeZones() const {
    return (shadeZones && !wattsArray.empty());
}

void
HrPwPlot::setShadeZones(int x)
{
    shadeZones = x;
}

void
HrPwPlot::refreshZoneLabels() {
    foreach(HrPwPlotZoneLabel *label, zoneLabels) {
        label->detach();
        delete label;
    }
    zoneLabels.clear();

    if (bg) {
        bg->detach();
        delete bg;
        bg = NULL;
    }

    if (rideItem) {
        int zone_range = rideItem->zoneRange();
        const Zones *zones = rideItem->zones;

        // generate labels for existing zones
        if (zones && (zone_range >= 0)) {
            int num_zones = zones->numZones(zone_range);
            for (int z = 0; z < num_zones; z ++) {
                HrPwPlotZoneLabel *label = new HrPwPlotZoneLabel(this, z);
                label->attach(this);
                zoneLabels.append(label);
            }
        }
    }

    // create a background object for shading
    bg = new HrPwPlotBackground(this);
    bg->attach(this);
}