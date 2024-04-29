#include "TrackBar.hpp"

#include "../../core/DrawContext.hpp"
#include "../../core/Batch2D.hpp"
#include "../../../assets/Assets.h"

using namespace gui;

TrackBar::TrackBar(
    double min, 
    double max, 
    double value, 
    double step, 
    int trackWidth
) : UINode(glm::vec2(26)), 
    min(min), 
    max(max), 
    value(value), 
    step(step), 
    trackWidth(trackWidth) 
{
    setColor(glm::vec4(0.f, 0.f, 0.f, 0.4f));
    setHoverColor(glm::vec4(0.01f, 0.02f, 0.03f, 0.5f));
}

void TrackBar::draw(const DrawContext* pctx, Assets* assets) {
    if (supplier) {
        value = supplier();
    }
    glm::vec2 pos = calcPos();
    auto batch = pctx->getBatch2D();
    batch->texture(nullptr);
    batch->setColor(hover ? hoverColor : color);
    batch->rect(pos.x, pos.y, size.x, size.y);

    float width = size.x - trackWidth;
    float t = (value - min) / (max-min);

    batch->setColor(trackColor);
    batch->rect(pos.x + width * t, pos.y, trackWidth, size.y);
}

void TrackBar::setSupplier(doublesupplier supplier) {
    this->supplier = supplier;
}

void TrackBar::setConsumer(doubleconsumer consumer) {
    this->consumer = consumer;
}

void TrackBar::mouseMove(GUI*, int x, int y) {
    glm::vec2 pos = calcPos();
    value = x - trackWidth/2;
    value -= pos.x;
    value = (value)/(size.x-trackWidth) * (max-min);
    value += min;
    value = (value > max) ? max : value;
    value = (value < min) ? min : value;
    value = (int64_t)round(value / step) * step;

    if (consumer) {
        consumer(value);
    }
}


double TrackBar::getValue() const {
    return value;
}

double TrackBar::getMin() const {
    return min;
}

double TrackBar::getMax() const {
    return max;
}

double TrackBar::getStep() const {
    return step;
}

int TrackBar::getTrackWidth() const {
    return trackWidth;
}

glm::vec4 TrackBar::getTrackColor() const {
    return trackColor;
}

void TrackBar::setValue(double x) {
    value = x;
}

void TrackBar::setMin(double x) {
    min = x;
}

void TrackBar::setMax(double x) {
    max = x;
}

void TrackBar::setStep(double x) {
    step = x;
}

void TrackBar::setTrackWidth(int width) {
    trackWidth = width;
}

void TrackBar::setTrackColor(glm::vec4 color) {
    trackColor = color;
}
