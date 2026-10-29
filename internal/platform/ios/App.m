#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>
#import <AVFAudio/AVFAudio.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include "ss_display.h"
#include "ss_haptics.h"
#include "ss_touch.h"
#include "ss_pointer.h"
#include "ss_health_bars.h"
#include "TouchLayout.h"

int ss_host_run(const char *directory);
void ss_input_set(uint64_t buttons, int32_t x, int32_t y);
void dm_focus_set(int on);
void dm_gfx_suspend(int on);
void dm_audio_suspend(int on);
void ss_display_set_hz(int hz);

/* SS_DISPLAY_HZ=120 in the game profile asks for 120 Hz on a ProMotion screen. The Info.plist key alone keeps
   an iPhone at 60 Hz, so the input display link asks for 80 to 120 Hz. The game starts at 60 Hz and follows the
   rate that the screen gives: 60 Hz in Low Power Mode, when the device is hot, or on a 60 Hz screen. */
static int displayHzWanted = 60;

/* TV mode follows the audio output when TouchableTVModeAudioRoute is set in Info.plist (only Rhythm Heaven).
   The built-in speaker, wired headphones, USB audio, and line out are handheld mode. Bluetooth,
   AirPlay, HDMI, and other outputs are TV mode, where the game offers its latency calibration. */
static BOOL tvModeAudioRoute;
void dm_opmode_set(int mode);
static BOOL audioRouteIsTV(AVAudioSessionRouteDescription *route) {
    NSSet *handheld = [NSSet setWithObjects:AVAudioSessionPortBuiltInSpeaker, AVAudioSessionPortBuiltInReceiver,
                                            AVAudioSessionPortHeadphones, AVAudioSessionPortUSBAudio, AVAudioSessionPortLineOut, nil];
    for (AVAudioSessionPortDescription *port in route.outputs) if (![handheld containsObject:port.portType]) return YES;
    return NO;
}
static NSString *audioRouteName(AVAudioSessionRouteDescription *route) {
    NSMutableArray *names = [NSMutableArray array];
    for (AVAudioSessionPortDescription *port in route.outputs) [names addObject:port.portType];
    return [names componentsJoinedByString:@","];
}
/* Switch the game while it runs, like docking a Switch (dm_opmode_set). Any thread. */
static void followAudioRoute(const char *why) {
    if (!tvModeAudioRoute) return;
    AVAudioSessionRouteDescription *route = AVAudioSession.sharedInstance.currentRoute;
    BOOL tv = audioRouteIsTV(route);
    fprintf(stderr, "TV mode: audio output %s -> %s (%s)\n", audioRouteName(route).UTF8String, tv ? "TV" : "handheld", why);
    dm_opmode_set(tv);
}

static CAMetalLayer *gameLayer;
static UIViewController *gameController;
static uint64_t touchButtons;
static uint64_t zoneButtons;
static uint64_t pointerButtons;
@class TouchZone;
static NSHashTable<TouchZone *> *touchZones;
static BOOL gameStarted;
static NSString *moduleDirectory;
/* Hide level controls as soon as touch + opens pause. Native frames clear this state outside levels. */
static BOOL pauseMenu;
static void notePress(uint64_t bit) {
    if (bit == 1ull << SS_BUTTON_A || bit == 1ull << SS_BUTTON_B) pauseMenu = NO;
    else if (bit == 1ull << 10) pauseMenu = !pauseMenu;
}

/* A new key on a real controller hides the Switch-style touch controls. A touch on the screen shows them again.
   The rhythm layout keeps its controls. */
static BOOL switchControls, controllerInUse;
static uint64_t controllerKeys;
enum { SS_STICK_KEYS = 3 };   /* pseudo keys 62 and 63: the left and right sticks moved past half way */
static void setControllerInUse(BOOL value, const char *reason) {
    if (!switchControls || controllerInUse == value) return;
    controllerInUse = value;
    fprintf(stderr, "Touch controls: %s (%s)\n", value ? "hidden" : "shown", reason);
}
static void noteControllerKeys(uint64_t keys) {
    if (keys & ~controllerKeys) setControllerInUse(YES, "real controller key");
    controllerKeys = keys;
}

static NSString *gameName(void) {
    return [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"] ?: @"Touchable";
}

void *front_metal_layer(void) { return (__bridge void *)gameLayer; }

static void showMessage(NSString *message) {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:gameName() message:message preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [gameController presentViewController:alert animated:YES completion:nil];
    });
}
void front_save_failed(int error) {
    showMessage(error == ENOSPC ? @"Not enough storage. This progress was not saved. Free up storage on the device." : @"This progress was not saved. Keep the log to find the cause.");
}

static void updateInput(void) {
    uint64_t mask = 0;
    int32_t x = 0, y = 0;
    for (GCController *controller in GCController.controllers) {
        GCExtendedGamepad *pad = controller.extendedGamepad;
        if (!pad) continue;
        /* Apple uses the bottom/right face positions for A/B. */
        if (pad.buttonB.pressed) mask |= 1ull << 0;
        if (pad.buttonA.pressed) mask |= 1ull << 1;
        if (pad.buttonY.pressed) mask |= 1ull << 2;
        if (pad.buttonX.pressed) mask |= 1ull << 3;
        if (pad.leftThumbstickButton.pressed) mask |= 1ull << 4;
        if (pad.rightThumbstickButton.pressed) mask |= 1ull << 5;
        if (pad.leftShoulder.pressed) mask |= 1ull << 6;
        if (pad.rightShoulder.pressed) mask |= 1ull << 7;
        if (pad.leftTrigger.pressed) mask |= 1ull << 8;
        if (pad.rightTrigger.pressed) mask |= 1ull << 9;
        if (pad.buttonMenu.pressed) mask |= 1ull << 10;
        if (pad.buttonOptions.pressed) mask |= 1ull << 11;
        if (pad.dpad.left.pressed) mask |= 1ull << 12;
        if (pad.dpad.up.pressed) mask |= 1ull << 13;
        if (pad.dpad.right.pressed) mask |= 1ull << 14;
        if (pad.dpad.down.pressed) mask |= 1ull << 15;
        if (fabsf(pad.leftThumbstick.xAxis.value) > 0.15f || fabsf(pad.leftThumbstick.yAxis.value) > 0.15f) {
            x = (int32_t)(pad.leftThumbstick.xAxis.value * 32767);
            y = (int32_t)(pad.leftThumbstick.yAxis.value * 32767);
        }
        if (hypot(pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value) > 0.5) mask |= 1ull << 62;
        if (hypot(pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value) > 0.5) mask |= 1ull << 63;
    }
    noteControllerKeys(mask);
    mask &= ~((uint64_t)SS_STICK_KEYS << 62);
    ss_input_set(mask | touchButtons | zoneButtons | pointerButtons, x, y);
}

/* The outline of a touch button. The user's sketch of 2026-10-23 makes ZL and ZR slanted and R a diamond. */
typedef NS_ENUM(NSInteger, PadShape) { PadShapeRound, PadShapeLeanLeft, PadShapeLeanRight, PadShapeDiamond };

/* The transform that gives a rounded rectangle of glass the outline of the shape. ZL leans like a
   backslash and ZR like a slash (a shear). R is a square turned 45 degrees. */
static CGAffineTransform padShapeTransform(PadShape shape, CGSize size) {
    CGFloat lean = size.height > 0 ? 0.22 * size.width / size.height : 0;
    if (shape == PadShapeLeanLeft) return CGAffineTransformMake(1, 0, lean, 1, 0, 0);
    if (shape == PadShapeLeanRight) return CGAffineTransformMake(1, 0, -lean, 1, 0, 0);
    if (shape == PadShapeDiamond) return CGAffineTransformMakeRotation(M_PI_4);
    return CGAffineTransformIdentity;
}

@interface PadButton : UIButton
@property(nonatomic) uint64_t bit;
@property(nonatomic) PadShape shape;   /* a transform gives the glass this outline */
@property(nonatomic, strong) UILabel *caption;   /* the upright title of a shaped button */
@property(nonatomic, strong) UIImageView *captionImage;   /* the upright symbol of a shaped button */
@property(nonatomic) BOOL appearanceHidden;   /* no look; the button still takes touches */
@property(nonatomic) BOOL faded;              /* an unused direction: an outline without glass */
@property(nonatomic) CGFloat hitMargin;   /* round touch area outside the button; 0 keeps the frame */
@property(nonatomic, copy) UIButtonConfiguration *look;   /* the glass look of a rhythm button */
@property(nonatomic, readonly) BOOL glassShown;
@property(nonatomic) BOOL slides;   /* a finger can slide from this button onto another sliding button */
@property(nonatomic, weak) PadButton *slideTarget;   /* the button that this button's touch now holds */
@property(nonatomic) BOOL untracked;   /* a slide ended UIKit's tracking of this button's touch */
@property(nonatomic) BOOL keepPress;   /* the game input keeps this button while UIKit's tracking ends */
@property(nonatomic) BOOL slideHeld;   /* a slide holds this button, so it shows the drawn mark */
@property(nonatomic, strong) UIView *slideMark;
@property(nonatomic) CGRect homeFrame, mapFrame;   /* the frames in the pad and on the zoomed map */
@end
@implementation PadButton
/* Place the button with its shape. A transformed view takes its place from bounds and center. */
- (void)placeAt:(CGPoint)center size:(CGSize)size {
    self.transform = CGAffineTransformIdentity;
    self.bounds = (CGRect){CGPointZero, size};
    self.center = center;
    self.transform = padShapeTransform(self.shape, size);
    [self setNeedsLayout];
}
/* The title of a shaped button stays upright: the caption undoes the button transform. */
- (void)setCaptionText:(NSString *)text font:(UIFont *)font {
    if (!self.caption) {
        self.caption = [UILabel new];
        self.caption.userInteractionEnabled = NO;
        self.caption.textColor = UIColor.whiteColor;
        self.caption.textAlignment = NSTextAlignmentCenter;
        [self addSubview:self.caption];
    }
    self.caption.text = text;
    self.caption.font = font;
    [self setNeedsLayout];
}
- (void)setCaptionSymbol:(UIImage *)image {
    if (!self.captionImage) {
        self.captionImage = [UIImageView new];
        self.captionImage.userInteractionEnabled = NO;
        self.captionImage.tintColor = UIColor.whiteColor;
        self.captionImage.contentMode = UIViewContentModeCenter;
        [self addSubview:self.captionImage];
    }
    self.captionImage.image = image;
    [self setNeedsLayout];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    for (UIView *view in @[self.caption ?: (id)NSNull.null, self.captionImage ?: (id)NSNull.null]) {
        if (![view isKindOfClass:UIView.class]) continue;
        [self bringSubviewToFront:view];
        view.transform = CGAffineTransformIdentity;
        [view sizeToFit];
        view.center = CGPointMake(CGRectGetMidX(self.bounds), CGRectGetMidY(self.bounds));
        view.transform = CGAffineTransformInvert(self.transform);
    }
}
/* A touch stays with the button where it started. That button stays pressed until the finger lifts.
   A slide also presses another sliding button when the finger enters its touch area, and releases the
   button of the previous slide. The touch areas do not overlap, so the touch holds the first button and
   at most one more. A slide back to the first button holds only that button. A button that another
   finger holds does not take the press. */
- (PadButton *)slideButtonFor:(UITouch *)touch {
    for (UIView *view in self.superview.subviews) {
        if (![view isKindOfClass:PadButton.class]) continue;
        PadButton *button = (PadButton *)view;
        if (button.slides && !button.hidden && button.userInteractionEnabled && [button pointInside:[touch locationInView:button] withEvent:nil])
            return button;
    }
    return nil;
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    UITouch *touch = touches.anyObject;
    PadButton *current = self.slideTarget ?: self;
    if (self.slides && touch && ![current pointInside:[touch locationInView:current] withEvent:event]) {
        PadButton *next = [self slideButtonFor:touch];
        if (next && next != current && (next == self || !next.highlighted)) {
            /* UIKit shows the system press look only on the button that tracks the touch.
               The first slide ends that tracking. Each button that a slide holds shows a drawn mark. */
            if (!self.untracked) {
                self.untracked = YES;
                self.keepPress = YES;
                [super touchesCancelled:touches withEvent:event];
                self.keepPress = NO;
                UIButtonConfiguration *config = self.configuration;   /* a new glass background ends the press look */
                self.configuration = nil;
                self.configuration = config;
                self.slideHeld = YES;
                self.highlighted = YES;   /* the cancel released the first button */
            }
            if (current != self) current.highlighted = NO;
            if (next != self) {
                next.slideHeld = YES;
                next.highlighted = YES;
            }
            self.slideTarget = next == self ? nil : next;
            if (getenv("SS_SLIDE_TRACE")) fprintf(stderr, "Touch slide: %s to %s\n", current.accessibilityLabel.UTF8String, next.accessibilityLabel.UTF8String);
            current = next;
        }
    }
    if (!self.untracked) [super touchesMoved:touches withEvent:event];
}
- (void)releaseSlide {
    PadButton *target = self.slideTarget;
    self.slideTarget = nil;
    if (target.highlighted) target.highlighted = NO;
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    self.untracked = NO;
    [super touchesBegan:touches withEvent:event];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self releaseSlide];
    if (self.untracked) self.highlighted = NO;
    else [super touchesEnded:touches withEvent:event];
    [self endPressLook];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self releaseSlide];
    if (self.untracked) self.highlighted = NO;
    else [super touchesCancelled:touches withEvent:event];
    [self endPressLook];
}
/* The system glass press response fades out over about 0.2 seconds after the release. A new glass
   background ends it at once (user request on 2026-10-23: a faster button response). */
- (void)endPressLook {
    if (!self.configuration) return;
    UIButtonConfiguration *config = self.configuration;
    self.configuration = nil;
    self.configuration = config;
}
/* The mark of a button that a slide holds: a white fill, as bright as the lit D-pad arm (DPAD_LIT_FILL). */
- (void)showSlideMark:(BOOL)shown {
    if (shown && !self.slideMark) {
        self.slideMark = [UIView new];
        self.slideMark.userInteractionEnabled = NO;
        self.slideMark.backgroundColor = [UIColor colorWithWhite:1 alpha:0.2];
        [self addSubview:self.slideMark];
    }
    self.slideMark.frame = self.bounds;
    /* A circle or a capsule. A shaped button has a rounded rectangle, which its transform shapes like the button. */
    self.slideMark.layer.cornerRadius = self.shape == PadShapeRound ? fmin(self.bounds.size.width, self.bounds.size.height) / 2 : 10;
    self.slideMark.hidden = !shown;
}
/* The touch area is the drawn circle or capsule, widened by hitMargin on each side. */
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    if (self.hitMargin <= 0) return [super pointInside:point withEvent:event];
    CGSize size = self.bounds.size;
    CGFloat radius = size.height / 2, x = fmin(fmax(point.x, radius), fmax(size.width - radius, radius));
    return hypot(point.x - x, point.y - radius) <= radius + self.hitMargin;
}
- (BOOL)glassShown { return !self.look || (!self.appearanceHidden && !self.faded); }
/* Apple's HIG: an alpha below 1 breaks glass. A hidden or faded rhythm button uses a plain look instead. */
- (void)updateLook {
    if (!self.look) return;
    UIButtonConfiguration *config = self.look;
    if (self.appearanceHidden) {
        config = [UIButtonConfiguration plainButtonConfiguration];
    } else if (self.faded) {
        config = [UIButtonConfiguration plainButtonConfiguration];
        config.image = self.look.image;
        config.preferredSymbolConfigurationForImage = self.look.preferredSymbolConfigurationForImage;
        config.attributedTitle = self.look.attributedTitle;
        config.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
        config.contentInsets = NSDirectionalEdgeInsetsZero;
        config.baseForegroundColor = [UIColor colorWithWhite:1 alpha:0.3];
        config.background.strokeColor = [UIColor colorWithWhite:1 alpha:0.25];
        config.background.strokeWidth = 1;
    }
    self.configuration = config;
    [self setNeedsUpdateConfiguration];
}
- (void)setLook:(UIButtonConfiguration *)look {
    _look = [look copy];
    [self updateLook];
}
- (void)setAppearanceHidden:(BOOL)value {
    if (_appearanceHidden == value) return;
    _appearanceHidden = value;
    if (value && self.highlighted) self.highlighted = NO;
    [self updateLook];
}
- (void)setFaded:(BOOL)value {
    if (_faded == value) return;
    _faded = value;
    [self updateLook];
}
- (void)setHighlighted:(BOOL)value {
    if (value && self.hidden) value = NO;   /* a touch that started before the button hid */
    BOOL was = self.highlighted;
    [super setHighlighted:value];
    if (!value) self.slideHeld = NO;
    if (self.slideHeld || self.slideMark) [self showSlideMark:self.slideHeld];
    if (value && !was) notePress(self.bit);
    if (value) touchButtons |= self.bit; else if (!self.keepPress) touchButtons &= ~self.bit;
    if (value != was && getenv("RH_MENU_TRACE") && (self.bit & ((1ull << 1) | (1ull << 3) | (1ull << 10) | (1ull << 11))))
        fprintf(stderr, "Touch button %s: %s mask %#llx\n", self.accessibilityLabel.UTF8String, value ? "down" : "up", (unsigned long long)self.bit);
    updateInput();
}
- (void)setHidden:(BOOL)hidden {
    if (hidden && self.highlighted) self.highlighted = NO;
    if (hidden) [self releaseSlide];
    [super setHidden:hidden];
}
@end

/* Liquid Glass for the touch controls. Apple's HIG uses the clear variant over rich media,
   such as a game. The controls share one glass container (UIGlassContainerEffect). */
static UIButtonConfiguration *glassButtonConfiguration(void) {
    UIButtonConfiguration *config = [UIButtonConfiguration clearGlassButtonConfiguration];
    config.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    config.contentInsets = NSDirectionalEdgeInsetsZero;
    config.baseForegroundColor = UIColor.whiteColor;
    return config;
}
/* The rhythm layout has a fixed size. The arrows use SF Symbols. */
static UIButtonConfiguration *rhythmGlassConfiguration(NSString *title, NSString *symbol) {
    UIButtonConfiguration *config = glassButtonConfiguration();
    if (symbol) {
        config.image = [UIImage systemImageNamed:symbol];
        config.preferredSymbolConfigurationForImage = [UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightBold];
    } else {
        config.attributedTitle = [[NSAttributedString alloc] initWithString:title attributes:@{NSFontAttributeName: [UIFont systemFontOfSize:20 weight:UIFontWeightSemibold]}];
    }
    return config;
}
/* A glass button shows Apple's own press response: the glass grows, lights up from under the finger,
   and the label dims ("Meet Liquid Glass", WWDC25). The touch controls add no light of their own. */
static void setGlassButton(UIButton *button, UIButtonConfiguration *config) {
    button.configuration = config;
}
static UIVisualEffectView *glassView(void) {
    UIVisualEffectView *view = [[UIVisualEffectView alloc] initWithEffect:[UIGlassEffect effectWithStyle:UIGlassEffectStyleClear]];
    view.cornerConfiguration = [UICornerConfiguration capsuleConfiguration];
    view.userInteractionEnabled = NO;
    return view;
}
/* The glass container takes no touches itself. Touches outside its controls go to the views below. */
@interface GlassLayer : UIVisualEffectView
@end
@implementation GlassLayer
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *view = [super hitTest:point withEvent:event];
    return view == self || view == self.contentView ? nil : view;
}
@end

/* A pressed D-pad arm: a white light inside the glass arm. The user found the build 17 values (0.32 fill,
   0.9 glow) too bright. A glow outside the glass did not match the arm shape, so the arm has none. */
#define DPAD_LIT_FILL 0.2
/* Sizes of the D-pad parts, as fractions of its width. */
typedef struct { CGFloat center, arm, reach; } dpad_shape;
static dpad_shape dpadShape(CGFloat size) { return (dpad_shape){size / 2, round(size * 0.17), round(size * 0.42)}; }

/* The ring, the pressed arms, and the arrows above the D-pad glass. */
@interface PadDpadMarks : UIView
@property(nonatomic) unsigned directions;   /* SS_DPAD_* bits */
@end
@implementation PadDpadMarks
- (void)setDirections:(unsigned)directions {
    if (directions == _directions) return;
    _directions = directions;
    [self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    CGContextRef g = UIGraphicsGetCurrentContext();
    CGFloat s = self.bounds.size.width;
    dpad_shape d = dpadShape(s);
    CGFloat c = d.center, h = d.arm, r = d.reach;
    /* The ring shows that the diagonals between the arms also take touches. */
    CGContextSetLineWidth(g, 1);
    CGContextSetStrokeColorWithColor(g, [UIColor colorWithWhite:1 alpha:0.18].CGColor);
    CGContextStrokeEllipseInRect(g, CGRectInset(self.bounds, 0.5, 0.5));
    /* A pressed arm lights inside the glass. The light has the shape of the glass bar, so it stays
       inside the glass edge. It starts at the other bar and fades in, so the place where the bars meet
       has no hard edge. A diagonal lights two arms, and the center stays unlit. */
    const unsigned bits[4] = {SS_DPAD_LEFT, SS_DPAD_UP, SS_DPAD_RIGHT, SS_DPAD_DOWN};
    const CGPoint out[4] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
    CGColorSpaceRef space = CGColorSpaceCreateDeviceGray();
    const CGFloat light[] = {1, 0, 1, DPAD_LIT_FILL, 1, DPAD_LIT_FILL};
    const CGFloat stops[] = {0, 0.35, 1};
    CGGradientRef gradient = CGGradientCreateWithColorComponents(space, light, stops, 3);
    for (int i = 0; i < 4; ++i) {
        if (!(self.directions & bits[i])) continue;
        CGPoint e = out[i];
        CGRect bar = e.x ? CGRectMake(c - r, c - h, 2 * r, 2 * h) : CGRectMake(c - h, c - r, 2 * h, 2 * r);
        CGRect arm = CGRectMake(e.x < 0 ? c - r : e.x > 0 ? c + h : c - h, e.y < 0 ? c - r : e.y > 0 ? c + h : c - h,
                                e.x ? r - h : 2 * h, e.y ? r - h : 2 * h);
        CGContextSaveGState(g);
        [[UIBezierPath bezierPathWithRoundedRect:bar cornerRadius:h] addClip];
        CGContextClipToRect(g, arm);
        CGContextDrawLinearGradient(g, gradient, CGPointMake(c + e.x * h, c + e.y * h), CGPointMake(c + e.x * r, c + e.y * r), 0);
        CGContextRestoreGState(g);
    }
    CGGradientRelease(gradient);
    CGColorSpaceRelease(space);
    /* An arrow in each arm points out from the center. */
    CGFloat a = round(s * 0.07), at = r - a * 1.6;
    for (int i = 0; i < 4; ++i) {
        CGPoint e = out[i], tip = CGPointMake(c + e.x * (at + a * 0.6), c + e.y * (at + a * 0.6));
        CGPoint base = CGPointMake(c + e.x * (at - a * 0.4), c + e.y * (at - a * 0.4));
        CGContextMoveToPoint(g, tip.x, tip.y);
        CGContextAddLineToPoint(g, base.x - e.y * a * 0.8, base.y - e.x * a * 0.8);
        CGContextAddLineToPoint(g, base.x + e.y * a * 0.8, base.y + e.x * a * 0.8);
        CGContextClosePath(g);
        CGContextSetFillColorWithColor(g, [UIColor colorWithWhite:1 alpha:self.directions & bits[i] ? 1 : 0.7].CGColor);
        CGContextFillPath(g);
    }
}
@end

/* The left D-pad. One finger selects one of 8 directions. A diagonal holds two D-pad buttons.
   The finger keeps control outside the drawn circle until it lifts. Two glass bars make the cross. */
@interface PadDpad : UIView
@property(nonatomic) CGFloat hitMargin;    /* a touch this far outside the circle also starts the D-pad */
@property(nonatomic) CGFloat centerSize;   /* a touch this near the center selects no direction */
@property(nonatomic) unsigned directions;  /* SS_DPAD_* bits */
@property(nonatomic, strong) UITouch *touch;
@property(nonatomic, strong) UIVisualEffectView *across, *down;
@property(nonatomic, strong) PadDpadMarks *marks;
- (void)cancelInput;
@end
@implementation PadDpad
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.accessibilityLabel = @"D-pad";
        _across = glassView();
        _down = glassView();
        _marks = [PadDpadMarks new];
        _marks.opaque = NO;
        _marks.backgroundColor = UIColor.clearColor;
        _marks.contentMode = UIViewContentModeRedraw;
        _marks.userInteractionEnabled = NO;
        [self addSubview:_across];
        [self addSubview:_down];
        [self addSubview:_marks];
    }
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    dpad_shape d = dpadShape(self.bounds.size.width);
    self.across.frame = CGRectMake(d.center - d.reach, d.center - d.arm, 2 * d.reach, 2 * d.arm);
    self.down.frame = CGRectMake(d.center - d.arm, d.center - d.reach, 2 * d.arm, 2 * d.reach);
    self.marks.frame = self.bounds;
}
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    CGFloat radius = self.bounds.size.width / 2;
    return hypot(point.x - radius, point.y - radius) <= radius + self.hitMargin;
}
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    return self.hidden || !self.userInteractionEnabled || ![self pointInside:point withEvent:event] ? nil : self;
}
- (void)setDirections:(unsigned)directions {
    if (directions == _directions) return;
    _directions = directions;
    self.marks.directions = directions;
    const uint64_t all = 0xfull << SS_BUTTON_LEFT;
    touchButtons = (touchButtons & ~all) | (uint64_t)directions << SS_BUTTON_LEFT;
    updateInput();
}
- (void)readTouch {
    if (self.hidden || !self.touch) return;
    CGPoint p = [self.touch locationInView:self];
    CGFloat radius = self.bounds.size.width / 2;
    self.directions = ss_dpad_directions(p.x - radius, p.y - radius, self.centerSize, self.directions);
}
- (void)cancelInput { self.touch = nil; self.directions = 0; }
- (void)setHidden:(BOOL)hidden {
    [super setHidden:hidden];
    if (hidden) [self cancelInput];   /* a touch that started before the D-pad hid */
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!self.touch) self.touch = touches.anyObject;
    [self readTouch];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { if ([touches containsObject:self.touch]) [self readTouch]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { if ([touches containsObject:self.touch]) [self cancelInput]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self touchesEnded:touches withEvent:event]; }
@end

@interface TouchFeedbackView : UIView
@property(nonatomic, strong) UILabel *caption;
@end
@implementation TouchFeedbackView
- (instancetype)init {
    if ((self = [super initWithFrame:CGRectMake(0, 0, 160, 160)])) {
        self.opaque = NO;
        self.userInteractionEnabled = NO;
        self.backgroundColor = UIColor.clearColor;
        _caption = [[UILabel alloc] initWithFrame:CGRectMake(60, 4, 40, 34)];
        _caption.text = @"A";
        _caption.textAlignment = NSTextAlignmentCenter;
        _caption.font = [UIFont systemFontOfSize:26 weight:UIFontWeightSemibold];
        _caption.textColor = [UIColor colorWithWhite:1 alpha:0.9];
        _caption.shadowColor = [UIColor colorWithWhite:0 alpha:0.6];
        _caption.shadowOffset = CGSizeMake(0, 1);
        [self addSubview:_caption];
    }
    return self;
}
- (void)drawRect:(CGRect)rect {
    CGFloat colors[] = {1, 1, 1, 0.24, 1, 1, 1, 0.10, 1, 1, 1, 0};
    CGFloat stops[] = {0, 0.42, 1};
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColorComponents(space, colors, stops, 3);
    CGPoint center = CGPointMake(self.bounds.size.width / 2, self.bounds.size.height / 2);
    CGContextDrawRadialGradient(UIGraphicsGetCurrentContext(), gradient, center, 0, center, 80, 0);
    CGGradientRelease(gradient);
    CGColorSpaceRelease(space);
}
@end

/* A large multi-touch area for one button. The button stays pressed while any finger is down in it. */
@interface TouchZone : UIView
@property(nonatomic) uint64_t bit;
@property(nonatomic) NSInteger active;
@property(nonatomic, strong) UILabel *label;
@property(nonatomic) BOOL appearanceHidden;
@property(nonatomic) BOOL feedbackEnabled;
@property(nonatomic, strong) NSMutableSet<NSValue *> *activeTouches;
@property(nonatomic, strong) NSMutableDictionary<NSValue *, TouchFeedbackView *> *feedbackViews;
- (void)cancelInput;
@end
@implementation TouchZone
- (instancetype)initWithBit:(uint64_t)bit title:(NSString *)title {
    if ((self = [super initWithFrame:CGRectZero])) {
        _bit = bit;
        _activeTouches = [NSMutableSet set];
        _feedbackViews = [NSMutableDictionary dictionary];
        self.multipleTouchEnabled = YES;
        self.layer.borderWidth = 1;
        self.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.10].CGColor;
        self.layer.cornerRadius = 18;
        _label = [UILabel new];
        _label.text = title;
        _label.textColor = [UIColor colorWithWhite:1 alpha:0.28];
        _label.font = [UIFont systemFontOfSize:34 weight:UIFontWeightBold];
        _label.textAlignment = NSTextAlignmentCenter;
        _label.userInteractionEnabled = NO;
        [self addSubview:_label];
        self.accessibilityLabel = title;
        if (!touchZones) touchZones = [NSHashTable weakObjectsHashTable];
        [touchZones addObject:self];
    }
    return self;
}
- (void)layoutSubviews { [super layoutSubviews]; self.label.frame = self.bounds; }
- (void)setAppearanceHidden:(BOOL)value {
    if (_appearanceHidden == value) return;
    _appearanceHidden = value;
    if (value) [self cancelInput];
    self.label.hidden = value;
    self.layer.borderWidth = value ? 0 : 1;
    self.backgroundColor = [UIColor colorWithWhite:1 alpha:!value && self.active ? 0.10 : 0];
}
- (void)setPressed:(BOOL)pressed {
    /* Both halves can hold A. Releasing one half must preserve the other half. */
    zoneButtons = 0;
    for (TouchZone *zone in touchZones) if (zone.active) zoneButtons |= zone.bit;
    self.backgroundColor = [UIColor colorWithWhite:1 alpha:pressed && !self.appearanceHidden ? 0.10 : 0];
    updateInput();
}
- (void)setHidden:(BOOL)hidden {
    if (hidden && !self.hidden) [self cancelInput];
    [super setHidden:hidden];
}
- (void)cancelInput {
    [self.activeTouches removeAllObjects];
    [self.feedbackViews removeAllObjects];
    for (UIView *view in self.subviews) {
        if ([view isKindOfClass:TouchFeedbackView.class]) {
            [view.layer removeAllAnimations];
            [view removeFromSuperview];
        }
    }
    self.active = 0;
    [self setPressed:NO];
}
- (void)moveFeedback:(TouchFeedbackView *)feedback touch:(UITouch *)touch {
    CGPoint point = [touch locationInView:self];
    feedback.center = point;
    CGFloat x = MAX(20 - self.frame.origin.x, MIN(point.x, self.superview.bounds.size.width - 20 - self.frame.origin.x));
    CGFloat y = MAX(point.y - 60, self.superview.safeAreaInsets.top + 20 - self.frame.origin.y);
    feedback.caption.center = CGPointMake(x - feedback.frame.origin.x, y - feedback.frame.origin.y);
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    BOOL wasPressed = self.activeTouches.count > 0;
    for (UITouch *touch in touches) {
        NSValue *key = [NSValue valueWithNonretainedObject:touch];
        [self.activeTouches addObject:key];
        if (self.feedbackEnabled) {
            TouchFeedbackView *feedback = [TouchFeedbackView new];
            feedback.caption.text = self.accessibilityLabel;
            self.feedbackViews[key] = feedback;
            [self addSubview:feedback];
            [self moveFeedback:feedback touch:touch];
            if (getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch %s feedback: down %.1f %.1f\n", self.accessibilityLabel.UTF8String, feedback.center.x, feedback.center.y);
        }
    }
    self.active = self.activeTouches.count;
    if (!wasPressed && self.active) notePress(self.bit);
    [self setPressed:self.active > 0];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        TouchFeedbackView *feedback = self.feedbackViews[[NSValue valueWithNonretainedObject:touch]];
        if (feedback) [self moveFeedback:feedback touch:touch];
    }
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        NSValue *key = [NSValue valueWithNonretainedObject:touch];
        [self.activeTouches removeObject:key];
        TouchFeedbackView *feedback = self.feedbackViews[key];
        [self.feedbackViews removeObjectForKey:key];
        if (feedback) {
            [UIView animateWithDuration:0.16 delay:0 options:UIViewAnimationOptionBeginFromCurrentState | UIViewAnimationOptionAllowUserInteraction animations:^{
                feedback.alpha = 0;
            } completion:^(BOOL finished) { [feedback removeFromSuperview]; }];
        }
    }
    self.active = self.activeTouches.count;
    [self setPressed:self.active > 0];
    if (self.feedbackEnabled && getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch %s feedback: up, active %ld, zones %#llx\n", self.accessibilityLabel.UTF8String, (long)self.active, (unsigned long long)zoneButtons);
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self cancelInput]; }
@end

/* The left area in the "rhythm" layout. The game adapter selects its buttons.
   The controller hides this area when it uses the compact D-pad buttons. */
@interface AdaptiveZone : UIView
@property(nonatomic) uint64_t hint;   /* layout and buttons in use (low 32 bits of the packed hint) */
@property(nonatomic) uint64_t mask;   /* buttons this area holds down */
@property(nonatomic, strong) NSMutableDictionary<NSValue *, NSNumber *> *touchBits;
@property(nonatomic, strong) NSArray<UILabel *> *labels;
@property(nonatomic, strong) UIView *divider;
@end
@implementation AdaptiveZone
static NSString *buttonSymbol(int button) {
    switch (button) {
    case SS_BUTTON_A: return @"A"; case SS_BUTTON_B: return @"B"; case SS_BUTTON_LEFT: return @"◀";
    case SS_BUTTON_UP: return @"▲"; case SS_BUTTON_RIGHT: return @"▶"; case SS_BUTTON_DOWN: return @"▼";
    }
    return @"";
}
- (instancetype)init {
    if ((self = [super initWithFrame:CGRectZero])) {
        self.multipleTouchEnabled = YES;
        self.layer.borderWidth = 1;
        self.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.10].CGColor;
        self.layer.cornerRadius = 18;
        _touchBits = [NSMutableDictionary dictionary];
        NSMutableArray *labels = [NSMutableArray array];
        for (int i = 0; i < 4; ++i) {
            UILabel *label = [UILabel new];
            label.textColor = [UIColor colorWithWhite:1 alpha:0.28];
            label.textAlignment = NSTextAlignmentCenter;
            label.userInteractionEnabled = NO;
            [self addSubview:label]; [labels addObject:label];
        }
        _labels = labels;
        _divider = [UIView new];
        _divider.backgroundColor = [UIColor colorWithWhite:1 alpha:0.10];
        _divider.userInteractionEnabled = NO;
        [self addSubview:_divider];
        _hint = ~0ull;
        [self applyHint:SS_TOUCH_MENU];
    }
    return self;
}
- (void)applyHint:(uint64_t)hint {
    hint &= 0xffffffffull;
    if (hint == self.hint) return;
    self.hint = hint;
    [self.touchBits removeAllObjects];   /* a held finger must not keep a button of the old scene */
    [self updateMask];
    [self setNeedsLayout];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGSize size = self.bounds.size;
    int layout = ss_touch_layout(self.hint);
    for (UILabel *label in self.labels) label.hidden = YES;
    self.divider.hidden = layout != SS_TOUCH_TWO;
    if (layout == SS_TOUCH_MENU) return;
    if (layout == SS_TOUCH_ONE) {
        self.labels[0].hidden = NO; self.labels[0].text = buttonSymbol(ss_touch_first(self.hint));
        self.labels[0].font = [UIFont systemFontOfSize:40 weight:UIFontWeightBold];
        self.labels[0].frame = self.bounds;
    } else if (layout == SS_TOUCH_TWO) {
        int buttons[2] = {ss_touch_first(self.hint), ss_touch_second(self.hint)};
        for (int i = 0; i < 2; ++i) {
            self.labels[i].hidden = NO; self.labels[i].text = buttonSymbol(buttons[i]);
            self.labels[i].font = [UIFont systemFontOfSize:36 weight:UIFontWeightBold];
            self.labels[i].frame = CGRectMake(i * size.width / 2, 0, size.width / 2, size.height);
        }
        self.divider.frame = CGRectMake(size.width / 2 - 0.5, 16, 1, size.height - 32);
    } else {
        int buttons[4] = {SS_BUTTON_UP, SS_BUTTON_DOWN, SS_BUTTON_LEFT, SS_BUTTON_RIGHT};
        CGPoint centers[4] = {{size.width / 2, size.height * 0.2}, {size.width / 2, size.height * 0.8},
                              {size.width * 0.2, size.height / 2}, {size.width * 0.8, size.height / 2}};
        for (int i = 0; i < 4; ++i) {
            self.labels[i].hidden = NO; self.labels[i].text = buttonSymbol(buttons[i]);
            self.labels[i].font = [UIFont systemFontOfSize:30 weight:UIFontWeightBold];
            self.labels[i].frame = CGRectMake(centers[i].x - 30, centers[i].y - 25, 60, 50);
        }
    }
}
- (int)buttonAt:(CGPoint)point {
    CGSize size = self.bounds.size;
    int layout = ss_touch_layout(self.hint);
    if (layout == SS_TOUCH_MENU) return -1;
    if (layout == SS_TOUCH_ONE) return ss_touch_first(self.hint);
    if (layout == SS_TOUCH_TWO) return point.x < size.width / 2 ? ss_touch_first(self.hint) : ss_touch_second(self.hint);
    CGFloat x = point.x - size.width / 2, y = point.y - size.height / 2;
    if (hypot(x, y) < 20) return -1;   /* the center picks no direction */
    if (fabs(x) > fabs(y)) return x < 0 ? SS_BUTTON_LEFT : SS_BUTTON_RIGHT;
    return y < 0 ? SS_BUTTON_UP : SS_BUTTON_DOWN;
}
- (void)updateMask {
    uint64_t mask = 0;
    for (NSNumber *button in self.touchBits.allValues) if (button.intValue >= 0) mask |= 1ull << button.intValue;
    uint64_t pressed = mask & ~self.mask;
    for (int bit = 0; pressed; ++bit, pressed >>= 1) if (pressed & 1) notePress(1ull << bit);
    touchButtons = (touchButtons & ~self.mask) | mask;
    self.mask = mask;
    self.backgroundColor = [UIColor colorWithWhite:1 alpha:mask ? 0.10 : 0];
    updateInput();
}
- (void)readTouches:(NSSet<UITouch *> *)touches {
    for (UITouch *touch in touches) self.touchBits[[NSValue valueWithNonretainedObject:touch]] = @([self buttonAt:[touch locationInView:self]]);
    [self updateMask];
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self readTouches:touches]; }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (ss_touch_layout(self.hint) == SS_TOUCH_ONE) return;
    NSMutableSet *known = [NSMutableSet set];   /* a finger that began before a scene change stays released */
    for (UITouch *touch in touches) if (self.touchBits[[NSValue valueWithNonretainedObject:touch]]) [known addObject:touch];
    [self readTouches:known];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) [self.touchBits removeObjectForKey:[NSValue valueWithNonretainedObject:touch]];
    [self updateMask];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self touchesEnded:touches withEvent:event]; }
@end

/* Menus receive positions. Visible button hints keep the normal hold/release behavior. */
@interface PointerView : UIView
@property(nonatomic) BOOL fullView;   /* the game picture fills the view; otherwise it has a 16:9 fit */
@property(nonatomic, strong) UITouch *menuTouch;
@property(nonatomic, strong) NSMutableDictionary<NSValue *, NSNumber *> *heldHints;
@property(nonatomic) uint64_t token;
- (void)refresh;
- (void)cancelInput;
@end
@implementation PointerView
- (instancetype)init {
    if ((self = [super initWithFrame:CGRectZero])) {
        self.multipleTouchEnabled = YES;
        self.heldHints = [NSMutableDictionary dictionary];
    }
    return self;
}
- (BOOL)locate:(CGPoint)point x:(float *)x y:(float *)y {
    CGSize size = self.bounds.size;
    if (!self.fullView) return ss_pointer_location(size.width, size.height, point.x, point.y, x, y);
    if (size.width <= 0 || size.height <= 0) return NO;
    *x = (float)(point.x / size.width * 1920);
    *y = (float)(point.y / size.height * 1080);
    return point.x >= 0 && point.y >= 0 && point.x < size.width && point.y < size.height;
}
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    ss_pointer_frame frame; float x, y;
    if (!ss_pointer_read(&frame) || ![self locate:point x:&x y:&y]) return NO;
    if (frame.mode == SS_POINTER_MENU || frame.mode == SS_POINTER_MAP) return YES;
    int target = ss_pointer_hit(&frame, x, y);
    return target >= 0 && (frame.targets[target].flags & SS_POINTER_BUTTON);
}
- (void)updateHints {
    pointerButtons = 0;
    for (NSNumber *value in self.heldHints.allValues) pointerButtons |= value.unsignedLongLongValue;
    updateInput();
}
- (void)cancelInput {
    if (self.menuTouch) ss_pointer_push((ss_pointer_event){self.token, SS_POINTER_CANCEL, 0, 0});
    self.menuTouch = nil;
    [self.heldHints removeAllObjects];
    [self updateHints];
}
- (void)refresh {
    ss_pointer_frame frame;
    uint64_t token = ss_pointer_read(&frame) ? frame.token : 0;
    if (token != self.token) { [self cancelInput]; self.token = token; }
}
- (void)sendTouch:(UITouch *)touch type:(int)type {
    float x = 0, y = 0;
    [self locate:[touch locationInView:self] x:&x y:&y];
    ss_pointer_push((ss_pointer_event){self.token, type, x, y});
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self refresh];
    ss_pointer_frame frame;
    if (!ss_pointer_read(&frame)) return;
    for (UITouch *touch in touches) {
        float x = 0, y = 0;
        if (![self locate:[touch locationInView:self] x:&x y:&y]) continue;
        int target = ss_pointer_hit(&frame, x, y);
        if (target >= 0 && (frame.targets[target].flags & SS_POINTER_BUTTON)) {
            int bit = frame.targets[target].button;
            if (bit >= 0 && bit < 64) {
                uint64_t mask = 1ull << bit;
                self.heldHints[[NSValue valueWithNonretainedObject:touch]] = @(mask);
                notePress(mask);
            }
        } else if (!self.menuTouch && (frame.mode == SS_POINTER_MENU || frame.mode == SS_POINTER_MAP)) {
            self.menuTouch = touch;
            [self sendTouch:touch type:SS_POINTER_DOWN];
        }
    }
    [self updateHints];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (self.menuTouch && [touches containsObject:self.menuTouch]) [self sendTouch:self.menuTouch type:SS_POINTER_MOVE];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        if (touch == self.menuTouch) { [self sendTouch:touch type:SS_POINTER_UP]; self.menuTouch = nil; }
        [self.heldHints removeObjectForKey:[NSValue valueWithNonretainedObject:touch]];
    }
    [self updateHints];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self cancelInput]; }
@end

static NSString *touchLayout(void) {
    return [NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableTouchLayout"] ?: @"switch";
}

/* Return the displayed game rectangle. Present uses the same 16:9 fit. */
static CGRect gameRect(CGSize size) {
    if (size.width <= 0 || size.height <= 0) return CGRectZero;
    const CGFloat aspect = 16.0 / 9.0;
    CGFloat width = size.width, height = width / aspect;
    if (height > size.height) {
        height = size.height;
        width = height * aspect;
    }
    if (fabs(size.width / size.height - aspect) < 0.01) {
        return CGRectMake(0, 0, size.width, size.height);
    }
    return CGRectMake((size.width - width) / 2, (size.height - height) / 2, width, height);
}

@interface MetalView : UIView
@end
@implementation MetalView
+ (Class)layerClass { return CAMetalLayer.class; }
@end

/* Enemy health bars from the game adapter (ss_health_bars.h), above the game picture. The game picture fills
   the view. The layers are kept from frame to frame. */
@interface HealthBarView : UIView
- (void)refreshHidden:(BOOL)hidden;
@end
@implementation HealthBarView {
    NSMutableArray<CALayer *> *_backs, *_fills;
}
- (void)refreshHidden:(BOOL)hidden {
    ss_health_bar bars[SS_HEALTH_BARS_MAX];
    uint64_t age = 0;
    int count = hidden ? 0 : ss_health_bars_read(bars, SS_HEALTH_BARS_MAX, &age);
    if (age > 150000000ull) count = 0;   /* no enemy update for 150 ms, for example a room change */
    if (!count && !_backs.count) return;
    if (!_backs) { _backs = [NSMutableArray new]; _fills = [NSMutableArray new]; }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    CGSize size = self.bounds.size;
    CGFloat top = self.safeAreaInsets.top + 6, height = 5;
    for (int i = 0; i < count; ++i) {
        if (i == (int)_backs.count) {
            CALayer *back = [CALayer layer], *fill = [CALayer layer];
            back.backgroundColor = [UIColor colorWithWhite:0 alpha:0.6].CGColor;
            back.borderColor = [UIColor colorWithWhite:1 alpha:0.55].CGColor;
            back.borderWidth = 0.5; back.cornerRadius = 3;
            fill.backgroundColor = [UIColor colorWithRed:0.86 green:0.18 blue:0.2 alpha:1].CGColor;
            fill.cornerRadius = 2;
            [self.layer addSublayer:back]; [self.layer addSublayer:fill];
            [_backs addObject:back]; [_fills addObject:fill];
        }
        /* The bar is as wide as the enemy, 36 to 160 points, and 8 points above its top. */
        CGFloat width = fmin(fmax(bars[i].width * size.width, 36), 160);
        CGFloat x = bars[i].x * size.width - width / 2, y = fmax(bars[i].y * size.height - 8 - height, top);
        _backs[i].frame = CGRectMake(x - 1, y - 1, width + 2, height + 2);
        _fills[i].frame = CGRectMake(x, y, width * fmin(fmax(bars[i].fill, 0), 1), height);
        _backs[i].hidden = _fills[i].hidden = NO;
    }
    for (NSUInteger i = count; i < _backs.count; ++i) _backs[i].hidden = _fills[i].hidden = YES;
    [CATransaction commit];
}
@end

static void *runGame(void *unused) {
    @autoreleasepool { pthread_setname_np("Touchable main"); ss_host_run(moduleDirectory.fileSystemRepresentation); }
    return NULL;
}

@interface GameViewController : UIViewController
@property(nonatomic, strong) MetalView *metalView;
@property(nonatomic, strong) HealthBarView *healthBars;   /* only when SS_HEALTH_BARS is set */
@property(nonatomic, strong) PadDpad *dpad;
@property(nonatomic, strong) GlassLayer *glassLayer;   /* the Switch-style controls, in one glass container */
@property(nonatomic, strong) CAShapeLayer *dimming;    /* darkens the game behind the visible controls */
@property(nonatomic) CGFloat titleSize;
@property(nonatomic, strong) NSMutableArray<PadButton *> *buttons;
@property(nonatomic, strong) NSMutableArray<TouchZone *> *zones;
@property(nonatomic, strong) AdaptiveZone *leftZone;
@property(nonatomic, strong) TouchZone *singleLeftZone;
@property(nonatomic) BOOL rhythmLayout;
@property(nonatomic, strong) PointerView *pointerView;
@property(nonatomic, strong) PadButton *backButton;
@property(nonatomic, strong) PadButton *skipButton;   /* A in a cutscene that the game can skip */
@property(nonatomic, strong) UIButton *padToggle;
@property(nonatomic) BOOL padInMenu;      /* the player showed the pad in a touch menu */
@property(nonatomic) BOOL touchMenu;      /* the pad is hidden for a touch menu */
@property(nonatomic) BOOL menuFrame;      /* the game shows a menu that takes touches */
@property(nonatomic) BOOL cutsceneFrame;  /* the game plays a cutscene */
@property(nonatomic) BOOL skipAvailable;  /* the cutscene frame has a skip target */
@property(nonatomic) BOOL mapFrame;       /* the game shows the zoomed map, which takes drags */
@property(nonatomic) BOOL mapPad;         /* the map shows only Y and A at the left edge, and X and B at the right edge */
@property(nonatomic) BOOL loadingScreen;  /* a loading screen: all touch controls are hidden */
@property(nonatomic) int pointerMode;
@property(nonatomic) BOOL levelControlsVisible;
@property(nonatomic) BOOL fullPad;   /* the scene uses the full controller (SS_TOUCH_PAD, Drum Lessons) */
@property(nonatomic) BOOL loggedControls;
@property(nonatomic) BOOL sideControlsFit;
@property(nonatomic) BOOL verticalBars;
@property(nonatomic, copy) NSString *loggedLayout;   /* the view size and insets of the last logged layout */
@property(nonatomic, strong) CADisplayLink *inputLink;
@property(nonatomic) int displayHz;          /* the rate given to the game */
@property(nonatomic) int displayHzTicks;     /* display frames in a row at another rate */
- (void)cancelTouches;
- (void)updateTouchMenu;
@end
@implementation GameViewController
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.view.multipleTouchEnabled = YES;
    self.metalView = [MetalView new];
    self.metalView.userInteractionEnabled = NO;
    [self.view addSubview:self.metalView];
    if (ss_health_bars_enabled()) {
        self.healthBars = [HealthBarView new];
        self.healthBars.userInteractionEnabled = NO;
        [self.view addSubview:self.healthBars];
    }
    gameLayer = (CAMetalLayer *)self.metalView.layer;
    gameLayer.device = MTLCreateSystemDefaultDevice();
    gameLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    gameLayer.framebufferOnly = YES;
    gameLayer.maximumDrawableCount = 3;
    self.buttons = [NSMutableArray new];
    self.zones = [NSMutableArray new];
    self.rhythmLayout = [touchLayout() isEqualToString:@"rhythm"];
    NSArray *names; const int *bits;
    static const int switchBits[] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 11};
    /* A stays transparent. B and +/- use the black side bars.
       Direction controls follow the current level frame. Y and X show when the level uses them.
       A visible A and ZR show only with the full controller. */
    static const int rhythmBits[] = {12, 13, 14, 15, 1, 11, 10, 3, 2, 0, 9};
    if (self.rhythmLayout) {
        TouchZone *a = [[TouchZone alloc] initWithBit:1ull << 0 title:@"A"];
        a.appearanceHidden = YES;
        a.feedbackEnabled = YES;
        self.leftZone = [AdaptiveZone new];
        self.leftZone.hidden = YES;
        self.singleLeftZone = [[TouchZone alloc] initWithBit:1ull << SS_BUTTON_A title:@"A"];
        self.singleLeftZone.appearanceHidden = YES;
        self.singleLeftZone.feedbackEnabled = YES;
        self.singleLeftZone.hidden = YES;
        [self.view addSubview:self.leftZone];
        [self.view addSubview:self.singleLeftZone];
        [self.view addSubview:a];
        [self.zones addObject:a];
        [self.zones addObject:self.singleLeftZone];
        names = @[@"◀", @"▲", @"▶", @"▼", @"B", @"−", @"+", @"Y", @"X", @"A", @"ZR"]; bits = rhythmBits;
    } else {
        /* Menu touches go to the game. The pad above it keeps its own touches. */
        switchControls = YES;
        self.pointerView = [PointerView new];
        self.pointerView.fullView = YES;
        [self.view addSubview:self.pointerView];
        [self addGlassLayer];
        self.dpad = [[PadDpad alloc] initWithFrame:CGRectMake(0, 0, 140, 140)];
        [self.glassLayer.contentView addSubview:self.dpad];
        names = @[@"A", @"B", @"X", @"Y", @"L", @"R", @"ZL", @"ZR", @"+", @"−"]; bits = switchBits;
    }
    if (self.rhythmLayout) {
        self.pointerView = [PointerView new];
        [self.view addSubview:self.pointerView];
        [self addGlassLayer];
    }
    UIView *controls = self.glassLayer.contentView;
    NSArray *arrows = @[@"arrowtriangle.left.fill", @"arrowtriangle.up.fill", @"arrowtriangle.right.fill", @"arrowtriangle.down.fill"];
    for (NSUInteger i = 0; i < names.count; ++i) {
        PadButton *button = [PadButton buttonWithType:UIButtonTypeCustom];
        button.bit = 1ull << bits[i];
        setGlassButton(button, glassButtonConfiguration());   /* the Switch title follows the layout size */
        button.accessibilityLabel = names[i];
        button.slides = !self.rhythmLayout && (i < 4 || i == SS_PAD_ZR);   /* A, B, X, Y, and ZR */
        if (!self.rhythmLayout && (i == SS_PAD_ZL || i == SS_PAD_ZR || i == SS_PAD_R || i == SS_PAD_L)) {
            button.shape = i == SS_PAD_ZL ? PadShapeLeanLeft : i == SS_PAD_ZR ? PadShapeLeanRight : PadShapeDiamond;
            UIButtonConfiguration *config = button.configuration;   /* a rounded rectangle of glass */
            config.cornerStyle = UIButtonConfigurationCornerStyleFixed;
            config.background.cornerRadius = 10;
            button.configuration = config;
        }
        if (self.rhythmLayout) {
            button.look = rhythmGlassConfiguration(names[i], i < 4 ? arrows[i] : nil);
            button.appearanceHidden = i < 4;
            button.hidden = i >= 7;
        }
        [controls addSubview:button]; [self.buttons addObject:button];
    }
    if (!self.rhythmLayout) {
        self.backButton = [PadButton buttonWithType:UIButtonTypeCustom];
        self.backButton.bit = 1ull << SS_BUTTON_B;
        self.backButton.accessibilityLabel = @"Back";
        self.skipButton = [PadButton buttonWithType:UIButtonTypeCustom];
        self.skipButton.bit = 1ull << SS_BUTTON_A;
        self.skipButton.accessibilityLabel = @"Skip";
        self.padToggle = [UIButton buttonWithType:UIButtonTypeCustom];
        self.padToggle.accessibilityLabel = @"Virtual controller";
        [self.padToggle addTarget:self action:@selector(togglePad) forControlEvents:UIControlEventTouchUpInside];
        [self styleMenuControl:self.backButton symbol:@"chevron.backward" selected:nil];
        [self styleMenuControl:self.skipButton symbol:@"forward.end.fill" selected:nil];
        [self styleMenuControl:self.padToggle symbol:@"gamecontroller" selected:@"gamecontroller.fill"];
        self.backButton.hidden = self.skipButton.hidden = self.padToggle.hidden = YES;
        [controls addSubview:self.backButton];
        [controls addSubview:self.skipButton];
        [controls addSubview:self.padToggle];
        /* Show the touch controls when the last real controller goes away. */
        [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidDisconnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
            for (GCController *controller in GCController.controllers) if (controller.extendedGamepad) return;
            setControllerInUse(NO, "real controller disconnected");
        }];
    }
    self.inputLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(pollInput:)];
    self.inputLink.preferredFrameRateRange = displayHzWanted > 60 ? CAFrameRateRangeMake(80, 120, 120) : CAFrameRateRangeMake(60, 60, 60);
    self.displayHz = 60;
    [self.inputLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
- (void)addGlassLayer {
    /* Apple's HIG adds a 35% dark layer behind clear glass when the content is bright. */
    UIView *dimView = [[UIView alloc] initWithFrame:self.view.bounds];
    dimView.userInteractionEnabled = NO;
    dimView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.dimming = [CAShapeLayer layer];
    self.dimming.fillColor = [UIColor colorWithWhite:0 alpha:0.35].CGColor;
    [dimView.layer addSublayer:self.dimming];
    [self.view addSubview:dimView];
    UIGlassContainerEffect *container = [UIGlassContainerEffect new];
    container.spacing = 0;   /* each control keeps its own glass shape */
    self.glassLayer = [[GlassLayer alloc] initWithEffect:container];
    self.glassLayer.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [self.view addSubview:self.glassLayer];
}
/* Give the game the rate that the screen runs at. A new rate must hold for 30 display frames. */
- (void)followDisplayRate:(CADisplayLink *)link {
    if (displayHzWanted <= 60) return;
    CFTimeInterval frame = link.targetTimestamp - link.timestamp;
    int hz = frame > 0 && frame < 1.0 / 90 ? 120 : 60;
    if (hz == self.displayHz) { self.displayHzTicks = 0; return; }
    if (++self.displayHzTicks < 30) return;
    self.displayHzTicks = 0;
    self.displayHz = hz;
    ss_display_set_hz(hz);
}
- (void)pollInput:(CADisplayLink *)link {
    [self followDisplayRate:link];
    [self.pointerView refresh];
    [self.healthBars refreshHidden:self.loadingScreen || self.menuFrame || self.cutsceneFrame || self.mapFrame];
    int controls = self.leftZone ? ss_pointer_controls() : SS_CONTROLS_SHOW;
    if (self.leftZone && controls == SS_CONTROLS_KEEP) {
        /* A short gap in the game's frames, for example a fade. Keep the controls as they are. */
    } else if (self.leftZone) {
        ss_pointer_frame frame;
        BOOL hasPointer = ss_pointer_read(&frame);
        if (hasPointer && frame.mode == SS_POINTER_HINTS && self.pointerMode == SS_POINTER_MENU) pauseMenu = NO;
        self.pointerMode = hasPointer ? frame.mode : SS_POINTER_NONE;
        uint64_t hint = ss_touch_left_get();
        if (!hasPointer || frame.mode != SS_POINTER_HINTS || ss_touch_layout(hint) == SS_TOUCH_MENU) pauseMenu = NO;
        BOOL inLevel = hasPointer && frame.mode == SS_POINTER_HINTS && !pauseMenu && ss_touch_layout(hint) != SS_TOUCH_MENU;
        /* Drum Lessons use the full controller. A touch menu, for example the pause menu, hides it. */
        BOOL fullPad = ss_touch_layout(hint) == SS_TOUCH_PAD && !pauseMenu &&
                       !(hasPointer && (frame.mode == SS_POINTER_MENU || frame.mode == SS_POINTER_MAP));
        if (fullPad != self.fullPad) {
            self.fullPad = fullPad;
            [self.zones.firstObject cancelInput];
            for (NSUInteger i = 0; i < 4; ++i) self.buttons[i].appearanceHidden = !fullPad && !self.levelControlsVisible;
            [self.view setNeedsLayout];
            if (getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch full controller: %s\n", fullPad ? "shown" : "hidden");
        }
        if (!inLevel) hint = SS_TOUCH_MENU;
        BOOL menu = ss_touch_layout(hint) == SS_TOUCH_MENU;
        BOOL dpad = ss_touch_layout(hint) == SS_TOUCH_DPAD;
        BOOL single = ss_touch_layout(hint) == SS_TOUCH_ONE;
        int extra = ss_touch_extra(hint);   /* zero outside a level */
        [self.leftZone applyHint:hint];
        self.leftZone.hidden = menu || dpad || single;
        if (single) {
            int button = ss_touch_first(hint);
            uint64_t bit = 1ull << button;
            if (self.singleLeftZone.bit != bit) {
                [self.singleLeftZone cancelInput];
                self.singleLeftZone.bit = bit;
                self.singleLeftZone.accessibilityLabel = buttonSymbol(button);
            }
        }
        if (self.singleLeftZone.hidden == single && getenv("RH_MENU_TRACE"))
            fprintf(stderr, "Touch left feedback: %s (button %s)\n", single ? "shown" : "hidden", self.singleLeftZone.accessibilityLabel.UTF8String);
        self.singleLeftZone.hidden = !single;
        BOOL directMenu = hasPointer && (frame.mode == SS_POINTER_MENU || frame.mode == SS_POINTER_MAP);
        TouchZone *a = self.zones.firstObject;
        a.hidden = directMenu || fullPad;
        if (!self.loggedControls || self.levelControlsVisible != inLevel) {
            if (self.levelControlsVisible && !inLevel) [a cancelInput];
            self.loggedControls = YES;
            self.levelControlsVisible = inLevel;
            for (NSUInteger i = 0; i < 4; ++i) self.buttons[i].appearanceHidden = !inLevel && !fullPad;
            if (getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch level controls: %s (pointer mode %d, scene layout %d, extra %#x)\n", inLevel ? "shown" : "hidden", self.pointerMode, ss_touch_layout(ss_touch_left_get()), ss_touch_extra(ss_touch_left_get()));
        }
        unsigned directions = ss_touch_first(hint);
        if (!directions) directions = SS_TOUCH_DIR_ALL;
        for (NSUInteger i = 0; i < 4; ++i) {   /* the small D-pad */
            PadButton *button = self.buttons[i];
            BOOL showDpad = dpad || (menu && !directMenu) || (extra & SS_TOUCH_SHOW_DPAD);
            if (!showDpad && button.highlighted) button.highlighted = NO;
            button.hidden = !showDpad;
            BOOL faded = dpad && !(directions & (1u << i));
            if (button.faded != faded) {
                button.faded = faded;
                if (getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch direction look: button %#llx %s\n", (unsigned long long)button.bit, faded ? "faded" : "glass");
            }
        }
        /* Y holds the tutorial demo button, or Y itself when the level uses it. */
        uint64_t demoBit = inLevel ? frame.tutorial_buttons : 0, yBit = demoBit ? demoBit : 1ull << 3;
        PadButton *y = self.buttons[7], *x = self.buttons[8];
        BOOL showY = (demoBit || (extra & SS_TOUCH_SHOW_Y)) && self.sideControlsFit;
        BOOL showX = (extra & SS_TOUCH_SHOW_X) && self.sideControlsFit;
        if (y.highlighted && (!showY || y.bit != yBit)) y.highlighted = NO;
        if (x.highlighted && !showX) x.highlighted = NO;
        y.bit = yBit;
        if (y.hidden == showY && getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch Y: %s (button %#llx)\n", showY ? "shown" : "hidden", (unsigned long long)y.bit);
        if (x.hidden == showX && getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch X: %s\n", showX ? "shown" : "hidden");
        y.hidden = !showY;
        x.hidden = !showX;
        if (fullPad) {
            self.leftZone.hidden = self.singleLeftZone.hidden = YES;
            for (NSUInteger i = 0; i < 4; ++i) self.buttons[i].hidden = NO;
            y.bit = 1ull << 3;
            y.hidden = x.hidden = NO;
        }
        for (NSUInteger i = 9; i < self.buttons.count; ++i) self.buttons[i].hidden = !fullPad || self.loadingScreen;   /* A, ZR */
        /* A loading screen hides the visible controls. The transparent A area stays. */
        [self noteLoadingScreen:controls == SS_CONTROLS_HIDE];
        if (self.loadingScreen) {
            for (NSUInteger i = 0; i < 4; ++i) self.buttons[i].hidden = YES;
            y.hidden = x.hidden = YES;
        }
        for (NSUInteger i = 4; i < 7; ++i) self.buttons[i].hidden = self.loadingScreen;   /* B, -, + */
        [self updateDimming];
    } else {
        [self updateTouchMenu];
    }
    updateInput();
}
- (void)styleMenuControl:(UIButton *)button symbol:(NSString *)symbol selected:(NSString *)selected {
    UIButtonConfiguration *config = glassButtonConfiguration();
    config.image = [UIImage systemImageNamed:symbol];
    config.preferredSymbolConfigurationForImage = [UIImageSymbolConfiguration configurationWithPointSize:20 weight:UIImageSymbolWeightSemibold];
    setGlassButton(button, config);
    if (selected) button.configurationUpdateHandler = ^(UIButton *control) {
        UIButtonConfiguration *updated = control.configuration;
        updated.image = [UIImage systemImageNamed:control.selected ? selected : symbol];
        control.configuration = updated;
    };
}
- (void)togglePad {
    self.padInMenu = !self.padInMenu;
    fprintf(stderr, "Touch menu: pad %s by the player\n", self.padInMenu ? "shown" : "hidden");
    [self updateTouchMenu];
}
/* Record a change of the loading screen state. */
- (void)noteLoadingScreen:(BOOL)loading {
    if (loading == self.loadingScreen) return;
    self.loadingScreen = loading;
    fprintf(stderr, "Touch loading: controls %s\n", loading ? "hidden" : "shown");
}
/* A menu that takes touches hides the pad. Back sends B. A cutscene also hides the pad; Skip sends A
   when the game can skip the cutscene. The zoomed map keeps only Y and A (at the left edge), and X and B (at the right edge);
   a drag moves the map. The toggle shows the pad again until the menu, the cutscene, or the map ends. A loading screen hides all controls. A short gap in the game's frames keeps the controls as
   they are. Without frames from the game, the pad shows. */
- (void)updateTouchMenu {
    int controls = ss_pointer_controls();
    if (controls != SS_CONTROLS_KEEP) {
        ss_pointer_frame frame;
        [self noteLoadingScreen:controls == SS_CONTROLS_HIDE];
        BOOL hasFrame = !self.loadingScreen && ss_pointer_read(&frame);
        self.menuFrame = hasFrame && frame.mode == SS_POINTER_MENU;
        BOOL cutscene = hasFrame && frame.mode == SS_POINTER_CUTSCENE, skip = cutscene && frame.count > 0;
        if (cutscene != self.cutsceneFrame || skip != self.skipAvailable)
            fprintf(stderr, "Touch cutscene: %s\n", !cutscene ? "ended" : skip ? "skip shown" : "no skip");
        self.cutsceneFrame = cutscene;
        self.skipAvailable = skip;
        self.mapFrame = hasFrame && frame.mode == SS_POINTER_MAP;
    }
    /* The pad stays hidden until the player shows it. The map keeps A, X, and B. */
    BOOL padFrame = self.menuFrame || self.cutsceneFrame || self.mapFrame;
    if (!padFrame) self.padInMenu = NO;
    BOOL mapPad = self.mapFrame && !self.padInMenu;
    BOOL touchMenu = padFrame && !self.padInMenu && !mapPad;
    if (mapPad != self.mapPad) {
        self.mapPad = mapPad;
        for (NSNumber *index in @[@(SS_PAD_A), @(SS_PAD_B), @(SS_PAD_X), @(SS_PAD_Y)]) {
            PadButton *button = self.buttons[index.unsignedIntegerValue];
            if (button.highlighted) button.highlighted = NO;
            button.frame = mapPad ? button.mapFrame : button.homeFrame;
        }
        fprintf(stderr, "Touch map: %s\n", mapPad ? "Y, A, X, and B only" : "pad as usual");
    }
    if (touchMenu != self.touchMenu) {
        self.touchMenu = touchMenu;
        fprintf(stderr, "Touch menu: %s\n", touchMenu ? "pad hidden" : "pad shown");
    }
    BOOL hidden = self.loadingScreen || controllerInUse;
    self.dpad.hidden = hidden || touchMenu || mapPad;
    for (NSUInteger i = 0; i < self.buttons.count; ++i) {
        BOOL mapButton = i == SS_PAD_A || i == SS_PAD_B || i == SS_PAD_X || i == SS_PAD_Y;
        self.buttons[i].hidden = hidden || touchMenu || (mapPad && !mapButton);
    }
    self.backButton.hidden = hidden || !touchMenu || !self.menuFrame;
    self.skipButton.hidden = hidden || !touchMenu || !self.skipAvailable;
    self.padToggle.hidden = hidden || !padFrame;
    self.padToggle.selected = self.padInMenu;
    [self updateDimming];
}
/* Put a dark circle behind each visible glass control. */
- (void)updateDimming {
    CGMutablePathRef path = CGPathCreateMutable();
    NSMutableArray<UIView *> *views = [NSMutableArray array];
    if (self.dpad) [views addObject:self.dpad];
    if (self.backButton) [views addObject:self.backButton];
    if (self.skipButton) [views addObject:self.skipButton];
    if (self.padToggle) [views addObject:self.padToggle];
    [views addObjectsFromArray:self.buttons];
    for (UIView *view in views) {
        BOOL glass = ![view isKindOfClass:PadButton.class] || ((PadButton *)view).glassShown;
        if (!view.hidden && glass) {   /* a circle, a capsule, or the transformed rectangle of a shaped button */
            BOOL shaped = [view isKindOfClass:PadButton.class] && ((PadButton *)view).shape != PadShapeRound;
            CGSize size = view.bounds.size;
            CGFloat radius = shaped ? 10 : fmin(size.width, size.height) / 2;
            CGAffineTransform place = CGAffineTransformMakeTranslation(view.center.x, view.center.y);
            place = CGAffineTransformConcat(CGAffineTransformTranslate(view.transform, -size.width / 2, -size.height / 2), place);
            CGPathAddRoundedRect(path, &place, view.bounds, radius, radius);
        }
    }
    if (!CGPathEqualToPath(path, self.dimming.path)) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        self.dimming.path = path;
        [CATransaction commit];
    }
    CGPathRelease(path);
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGSize size = self.view.bounds.size;
    ss_display_set_view(size.width, size.height);
    ss_display_size render = ss_display_resolution(720);
    self.metalView.frame = self.view.bounds;
    self.healthBars.frame = self.view.bounds;
    self.pointerView.frame = self.view.bounds;
    gameLayer.drawableSize = CGSizeMake(render.width, render.height);
    if ([[NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableNativeDrawable"] boolValue]) {
        CGFloat scale = self.view.window.windowScene.screen.nativeScale ?: self.traitCollection.displayScale;
        gameLayer.drawableSize = CGSizeMake(round(size.width * scale), round(size.height * scale));
    }
    UIEdgeInsets safe = self.view.safeAreaInsets;
    CGFloat left = MAX(safe.left + 12, 28), right = size.width - MAX(safe.right + 12, 28);
    CGFloat bottom = size.height - MAX(safe.bottom + 18, 36);
    if (self.rhythmLayout) {
        CGRect game = gameRect(size);
        self.verticalBars = game.origin.y >= 44 && size.height - CGRectGetMaxY(game) >= 44;
        CGFloat contentLeft = self.verticalBars ? game.origin.x : 0;
        CGFloat contentRight = self.verticalBars ? CGRectGetMaxX(game) : size.width;
        CGFloat contentBottom = self.verticalBars ? CGRectGetMaxY(game) : size.height;
        CGFloat top = MAX(safe.top, 0) + 60, half = (contentLeft + contentRight) / 2;
        self.zones[0].frame = CGRectMake(half + 4, top, contentRight - half - 8, contentBottom - top - 6);
        self.leftZone.frame = CGRectMake(contentLeft + 4, top, half - contentLeft - 8, contentBottom - top - 6);
        self.singleLeftZone.frame = self.leftZone.frame;
        CGFloat controlBottom = self.verticalBars ? contentBottom : bottom;
        CGFloat cx = left + 74, cy = controlBottom - 74, d = 54;
        CGFloat bar = (size.width - MIN(size.width, size.height * 16 / 9)) / 2;
        self.sideControlsFit = bar >= 52 || self.verticalBars;
        CGFloat lx = bar >= 52 ? bar / 2 : left + 30;
        CGFloat rx = bar >= 52 ? size.width - bar / 2 : right - 30;
        CGFloat by = MAX(safe.top + 30, 48);
        CGFloat fx = right - 74;   /* the face buttons of the full controller mirror the D-pad */
        CGPoint centers[] = {{cx - d, cy}, {cx, cy - d}, {cx + d, cy}, {cx, cy + d},
                             {lx, by}, {rx, by + 56}, {rx, by}, {lx, by + 56}, {lx, by + 112},
                             {fx + d, cy}, {fx + d / 2, cy - 2 * d - 8}};
        if (self.verticalBars) {
            CGFloat topY = MAX(safe.top + 22, 22);
            topY = MIN(topY, game.origin.y / 2);
            CGFloat pair = 50;
            centers[4] = (CGPoint){lx, topY};       /* B */
            centers[7] = (CGPoint){lx + pair, topY}; /* Y */
            centers[8] = (CGPoint){lx + 2 * pair, topY}; /* X */
            centers[5] = (CGPoint){rx - pair, topY}; /* - */
            centers[6] = (CGPoint){rx, topY};        /* + */
        }
        if (self.fullPad) {
            /* The full controller: A, B, X, and Y in a diamond at the lower right, and ZR above A and X. */
            centers[4] = (CGPoint){fx, cy + d};       /* B */
            centers[7] = (CGPoint){fx - d, cy};       /* Y */
            centers[8] = (CGPoint){fx, cy - d};       /* X */
        }
        self.glassLayer.frame = self.view.bounds;
        for (NSUInteger i = 0; i < self.buttons.count; ++i) {
            BOOL face = self.fullPad && (i == 4 || i >= 7);
            CGFloat s = i < 4 || face ? 50 : 44, w = i == 10 ? 64 : s;
            PadButton *button = self.buttons[i];
            button.frame = CGRectMake(centers[i].x - w / 2, centers[i].y - s / 2, w, s);
            if (i >= 4) button.appearanceHidden = !self.sideControlsFit && !face;
        }
        [self updateDimming];
        if (getenv("RH_MENU_TRACE")) fprintf(stderr, "Touch side bars: width %.1f B %s Y %s X %s minus %s plus %s; A area %s\n", (double)bar,
            NSStringFromCGRect(self.buttons[4].frame).UTF8String, NSStringFromCGRect(self.buttons[7].frame).UTF8String,
            NSStringFromCGRect(self.buttons[8].frame).UTF8String,
            NSStringFromCGRect(self.buttons[5].frame).UTF8String, NSStringFromCGRect(self.buttons[6].frame).UTF8String,
            NSStringFromCGRect(self.zones[0].frame).UTF8String);
        return;
    }
    BOOL padIdiom = self.traitCollection.userInterfaceIdiom == UIUserInterfaceIdiomPad;
    /* The iPadOS window controls take the upper-left corner. */
    UIViewLayoutRegion *belowWindowControls = [UIViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:UIViewLayoutRegionAdaptivityAxisVertical];
    CGFloat windowTop = [self.view edgeInsetsForLayoutRegion:belowWindowControls].top;
    ss_pad_layout pad = ss_pad_layout_make(size.width, size.height, safe.left, safe.right, safe.top, safe.bottom, windowTop, padIdiom);
    CGFloat dpad = pad.dpad.size;
    self.dpad.frame = CGRectMake(pad.dpad.x - dpad / 2, pad.dpad.y - dpad / 2, dpad, dpad);
    self.dpad.hitMargin = pad.dpad_margin;
    self.dpad.centerSize = pad.dpad_center;
    self.glassLayer.frame = self.view.bounds;
    CGFloat titleSize = round(pad.buttons[SS_PAD_A].size * 0.37);
    UIFont *font = [UIFont systemFontOfSize:titleSize weight:UIFontWeightSemibold];
    for (NSUInteger i = 0; i < self.buttons.count && i < SS_PAD_BUTTONS; ++i) {
        ss_pad_circle c = pad.buttons[i];
        PadButton *button = self.buttons[i];
        CGFloat w = ss_pad_width(c), h = c.size;   /* capsules and circles; ZL, ZR, and R are shaped */
        if (button.shape == PadShapeDiamond) w = h = c.size / M_SQRT2;   /* the layout circle is around the diamond */
        if (button.shape != PadShapeRound) [button placeAt:CGPointMake(c.x, c.y) size:CGSizeMake(w, h)];
        else button.frame = CGRectMake(c.x - w / 2, c.y - h / 2, w, h);
        /* The zoomed map moves Y above A at the left edge, and X above B at the right edge. The edges
           are those of − and +. A and B keep the height of B. The upper button is just clear of the
           lower button's touch area. */
        button.homeFrame = button.frame;
        if (i == SS_PAD_A || i == SS_PAD_B || i == SS_PAD_X || i == SS_PAD_Y) {
            ss_pad_circle minus = pad.buttons[SS_PAD_MINUS], plus = pad.buttons[SS_PAD_PLUS];
            BOOL leftEdge = i == SS_PAD_A || i == SS_PAD_Y, upper = i == SS_PAD_X || i == SS_PAD_Y;
            CGFloat x = leftEdge ? minus.x - minus.size / 2 : plus.x + plus.size / 2 - c.size;
            CGFloat y = pad.buttons[SS_PAD_B].y - (upper ? c.size + 2 * pad.hit_margin + 6 : 0) - c.size / 2;
            button.mapFrame = CGRectMake(x, y, c.size, c.size);
            if (self.mapPad) button.frame = button.mapFrame;
        }
        button.hitMargin = pad.hit_margin;
        if (titleSize != self.titleSize) {
            UIButtonConfiguration *config = button.configuration;
            if (i == SS_PAD_L) {   /* L opens the map. It shows an upright map symbol in its diamond. */
                config.attributedTitle = nil;
                config.image = nil;
                UIImageSymbolConfiguration *symbol = [UIImageSymbolConfiguration configurationWithPointSize:titleSize * 0.85 weight:UIImageSymbolWeightSemibold];
                [button setCaptionSymbol:[UIImage systemImageNamed:@"map.fill" withConfiguration:symbol]];
            } else if (button.shape != PadShapeRound) {   /* the caption keeps the title upright */
                config.attributedTitle = nil;
                [button setCaptionText:button.accessibilityLabel font:font];
            } else {
                config.attributedTitle = [[NSAttributedString alloc] initWithString:button.accessibilityLabel attributes:@{NSFontAttributeName: font}];
            }
            button.configuration = config;
        }
    }
    self.titleSize = titleSize;
    /* Skip takes the place of +, which a cutscene hides. */
    const ss_pad_circle menuControls[] = {pad.back, pad.pad_toggle, pad.buttons[SS_PAD_PLUS]};
    UIButton *menuButtons[] = {self.backButton, self.padToggle, self.skipButton};
    for (int i = 0; i < 3; ++i) {
        ss_pad_circle c = menuControls[i];
        menuButtons[i].frame = CGRectMake(c.x - c.size / 2, c.y - c.size / 2, c.size, c.size);
    }
    self.backButton.hitMargin = self.skipButton.hitMargin = pad.hit_margin;
    [self updateDimming];
    /* The safe area can change after the first layout. Log each new size or inset. */
    NSString *layoutKey = [NSString stringWithFormat:@"%@ %@ %.1f", NSStringFromCGSize(size), NSStringFromUIEdgeInsets(safe), (double)windowTop];
    if (![self.loggedLayout isEqualToString:layoutKey]) {
        self.loggedLayout = layoutKey;
        fprintf(stderr, "Touch layout %s: view %.0fx%.0f, safe %.0f/%.0f/%.0f/%.0f, window top %.0f, D-pad %s, A %s, ZR %s, ZL %s, minus %s, plus %s, back %s, pad toggle %s\n",
            pad.tablet ? "tablet" : "phone", (double)size.width, (double)size.height,
            (double)safe.left, (double)safe.right, (double)safe.top, (double)safe.bottom, (double)windowTop,
            NSStringFromCGRect(self.dpad.frame).UTF8String, NSStringFromCGRect(self.buttons[SS_PAD_A].frame).UTF8String,
            NSStringFromCGRect(self.buttons[SS_PAD_ZR].frame).UTF8String, NSStringFromCGRect(self.buttons[SS_PAD_ZL].frame).UTF8String,
            NSStringFromCGRect(self.buttons[SS_PAD_MINUS].frame).UTF8String, NSStringFromCGRect(self.buttons[SS_PAD_PLUS].frame).UTF8String,
            NSStringFromCGRect(self.backButton.frame).UTF8String, NSStringFromCGRect(self.padToggle.frame).UTF8String);
    }
}
- (void)cancelTouches {
    for (PadButton *button in self.buttons) if (button.highlighted) button.highlighted = NO;
    if (self.backButton.highlighted) self.backButton.highlighted = NO;
    if (self.skipButton.highlighted) self.skipButton.highlighted = NO;
    [self.dpad cancelInput];
    for (TouchZone *zone in self.zones) [zone cancelInput];
    [self.leftZone applyHint:SS_TOUCH_MENU];
    [self.pointerView cancelInput];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (gameStarted) return;
    gameStarted = YES;
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    pthread_t thread; int error = pthread_create(&thread, &attr, runGame, NULL); pthread_attr_destroy(&attr);
    if (error) { showMessage(@"Cannot start the game thread. Open the app again."); return; }
    pthread_detach(thread);
}
@end

/* A new touch on the screen shows the touch controls after a real controller hid them. */
@interface GameWindow : UIWindow
@end
@implementation GameWindow
- (void)sendEvent:(UIEvent *)event {
    if (controllerInUse && event.type == UIEventTypeTouches) {
        for (UITouch *touch in event.allTouches) {
            if (touch.phase != UITouchPhaseBegan) continue;
            setControllerInUse(NO, "screen touch");
            [(GameViewController *)gameController updateTouchMenu];
            break;
        }
    }
    [super sendEvent:event];
}
@end

@interface GameScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation GameScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    self.window = [[GameWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    gameController = [GameViewController new]; self.window.rootViewController = gameController;
    [self.window makeKeyAndVisible]; UIApplication.sharedApplication.idleTimerDisabled = YES;
}
- (void)sceneWillResignActive:(UIScene *)scene {
    [(GameViewController *)gameController cancelTouches];
    touchButtons = zoneButtons = pointerButtons = 0; ss_pointer_clear(); ss_input_set(0, 0, 0);
    if (gameStarted) { dm_focus_set(0); dm_audio_suspend(1); dm_gfx_suspend(1); }
}
- (void)sceneDidBecomeActive:(UIScene *)scene {
    if (gameStarted) { dm_gfx_suspend(0); dm_audio_suspend(0); dm_focus_set(1); followAudioRoute("app active"); }
}
@end

@interface GameDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation GameDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    tvModeAudioRoute = [[NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableTVModeAudioRoute"] boolValue];
    BOOL launchModeSet = getenv("DM_OPMODE") != NULL;   /* devicectl or Xcode launch environment */
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *bundle = NSBundle.mainBundle.resourcePath;
    NSString *gameID = [NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableGameID"];
    NSString *saveDirectory = [NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableSaveDirectory"];
    NSArray *localizations = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleLocalizations"];
    NSArray *appleLanguages = nil;
    NSArray *preferredLanguages = nil;
    NSArray *bundleLocalizations = nil;
    NSString *language = nil;
    /* Keep the prior runtime language for games without a per-app language list. */
    if ([localizations isKindOfClass:NSArray.class] && localizations.count) {
        /* The system stores the per-app language in AppleLanguages. NSLocale.preferredLanguages
           can keep the system language when only the script changes, such as zh-Hant to zh-Hans. */
        appleLanguages = [[NSUserDefaults standardUserDefaults] arrayForKey:@"AppleLanguages"];
        for (NSString *value in appleLanguages) {
            if ([value isKindOfClass:NSString.class] && value.length) { language = value; break; }
        }
        preferredLanguages = NSLocale.preferredLanguages;
        bundleLocalizations = NSBundle.mainBundle.preferredLocalizations;
        if (!language) language = bundleLocalizations.firstObject;
        if (!language) language = preferredLanguages.firstObject;
        if (!language) language = @"zh-Hans";
        setenv("DM_LANG", language.UTF8String, 1);
    }
    NSDictionary *directories = @{@"DM_SAVE": [[documents stringByAppendingPathComponent:@"Saves"] stringByAppendingPathComponent:saveDirectory],
        @"DM_TEMP": [NSTemporaryDirectory() stringByAppendingPathComponent:gameID],
        @"SS_SHADER_CAPTURE": [documents stringByAppendingPathComponent:@"ShaderCapture"]};
    for (NSString *key in directories) {
        NSString *path = directories[key]; [fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
        setenv(key.UTF8String, path.fileSystemRepresentation, 1);
    }
    NSString *logs = [documents stringByAppendingPathComponent:@"Logs"];
    [fm createDirectoryAtPath:logs withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *log = [logs stringByAppendingPathComponent:@"game.log"];
    [fm removeItemAtPath:[logs stringByAppendingPathComponent:@"previous.log"] error:nil];
    [fm moveItemAtPath:log toPath:[logs stringByAppendingPathComponent:@"previous.log"] error:nil];
    freopen(log.fileSystemRepresentation, "w", stderr); setvbuf(stderr, NULL, _IOLBF, 0);
    dup2(fileno(stderr), fileno(stdout)); setvbuf(stdout, NULL, _IOLBF, 0);
    if (language) {
        fprintf(stderr, "Preferred language: %s\n", language.UTF8String);
        NSLog(@"Language values: AppleLanguages=%@, preferredLanguages=%@, bundlePreferredLocalizations=%@",
              appleLanguages, preferredLanguages, bundleLocalizations);
    }
    moduleDirectory = [bundle stringByAppendingPathComponent:@"Modules"];
    setenv("DM_ROMFS", [bundle stringByAppendingPathComponent:@"RomFS"].fileSystemRepresentation, 1);
    setenv("DM_NVN_SHMAP", [bundle stringByAppendingPathComponent:@"Shaders/shader_map.tsv"].fileSystemRepresentation, 1);
    setenv("DM_MSL_DIR", [bundle stringByAppendingPathComponent:@"Shaders/ios"].fileSystemRepresentation, 1);
    setenv("DM_PSO_MANIFEST", "off", 1); setenv("DM_NVN_TBDR", "0", 1); setenv("DM_PRESENT_LINK", "0", 1);
    setenv("DM_AUDIO", "1", 1);
    NSDictionary *environment = [NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableRuntimeEnvironment"];
    for (NSString *key in environment) {
        setenv(key.UTF8String, [environment[key] UTF8String], 1);
    }
    /* Player choices on the app page in the Settings app replace the profile values. Only the values
       that the game lists in TouchablePlayerSettings are used. "default" keeps the profile value. */
    NSDictionary *playerSettings = [NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchablePlayerSettings"];
    if ([playerSettings isKindOfClass:NSDictionary.class]) {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        NSString *height = [defaults stringForKey:@"TouchableRenderHeight"], *rate = [defaults stringForKey:@"TouchableDisplayHz"];
        NSArray *heights = playerSettings[@"RenderHeights"], *rates = playerSettings[@"DisplayHz"];
        if (height.integerValue && [heights isKindOfClass:NSArray.class] && [heights containsObject:@(height.integerValue)])
            setenv("SS_RENDER_HEIGHT", height.UTF8String, 1);
        if (rate.integerValue && [rates isKindOfClass:NSArray.class] && [rates containsObject:@(rate.integerValue)])
            setenv("SS_DISPLAY_HZ", rate.UTF8String, 1);
        if ([playerSettings[@"HealthBars"] boolValue] && [defaults boolForKey:@"TouchableHealthBars"]) setenv("SS_HEALTH_BARS", "1", 1);
        fprintf(stderr, "Player settings: render height %s (SS_RENDER_HEIGHT=%s), refresh rate %s (SS_DISPLAY_HZ=%s), health bars %s\n",
                (height ?: @"default").UTF8String, getenv("SS_RENDER_HEIGHT") ?: "720",
                (rate ?: @"default").UTF8String, getenv("SS_DISPLAY_HZ") ?: "60", getenv("SS_HEALTH_BARS") ? "on" : "off");
    }
    /* The game starts at 60 Hz. The input display link raises the rate when the screen runs at 120 Hz. */
    const char *displayHz = getenv("SS_DISPLAY_HZ");
    if (displayHz && atoi(displayHz) == 120) displayHzWanted = 120;
    setenv("SS_DISPLAY_HZ", "60", 1);
    AVAudioSession *audio = AVAudioSession.sharedInstance;
    [audio setCategory:AVAudioSessionCategoryPlayback error:nil];
    NSNumber *ioBuffer = [NSBundle.mainBundle objectForInfoDictionaryKey:@"TouchableAudioIOBufferDuration"];
    if (ioBuffer) [audio setPreferredIOBufferDuration:ioBuffer.doubleValue error:nil];
    [audio setActive:YES error:nil];
    if (tvModeAudioRoute) {
        /* The start mode comes from the audio output. A launch environment (DM_OPMODE from devicectl) wins for tests. */
        AVAudioSessionRouteDescription *route = audio.currentRoute;
        if (!launchModeSet) setenv("DM_OPMODE", audioRouteIsTV(route) ? "1" : "0", 1);
        fprintf(stderr, "TV mode: launch %s, audio output %s\n", atoi(getenv("DM_OPMODE") ?: "0") ? "TV" : "handheld", audioRouteName(route).UTF8String);
        [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification object:audio queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
            followAudioRoute("route change");
        }];
    }
    ss_haptics_ios_init();
    [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionInterruptionNotification object:audio queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
        BOOL paused = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan;
        dm_audio_suspend(paused);
    }];
    fprintf(stderr, "Touchable %s build %s %s; iOS %s\n", gameID.UTF8String, __DATE__, __TIME__, UIDevice.currentDevice.systemVersion.UTF8String);
    return YES;
}
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Touchable" sessionRole:session.role];
    config.delegateClass = GameScene.class; return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(GameDelegate.class)); }
}
