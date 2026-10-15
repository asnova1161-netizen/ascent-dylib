// ===========================================================================
//  ascent-ios / overlay.mm
//  Transparent UIWindow above the game + a CADisplayLink-driven view that
//  projects player positions with the scanned camera and draws 2D boxes.
//  No ImGui / Metal hook — plain CoreGraphics keeps this small and robust.
// ===========================================================================
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

#include "game.hpp"
#include "math.hpp"

#include <atomic>

// --- tiny config (the loader / other AI can override later) ----------------
struct EspConfig {
    std::atomic<bool> enabled{ true };
    std::atomic<bool> boxes{ true };
    std::atomic<bool> filled{ false };
    std::atomic<bool> distance{ true };
    std::atomic<bool> status{ false };
    std::atomic<bool> snapline{ false };
};
static EspConfig g_cfg;

extern std::atomic<float> g_screenW;
extern std::atomic<float> g_screenH;

// ---------------------------------------------------------------------------
@interface AscentDrawView : UIView
@end

@implementation AscentDrawView

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.opaque = NO;
        self.userInteractionEnabled = NO;
        self.contentMode = UIViewContentModeRedraw;
    }
    return self;
}

- (void)drawRect:(CGRect)rect {
    if (!g_cfg.enabled.load()) return;

    auto snap = gameSnapshot();
    if (!snap || !snap->hasCamera) return;

    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) return;

    const CGFloat W = self.bounds.size.width;
    const CGFloat H = self.bounds.size.height;

    CGContextSetLineWidth(ctx, 1.0);

    for (const auto& p : snap->players) {
        Vec2 feet, head;
        if (!snap->camera.w2s(p.pos, feet, (float)W, (float)H)) continue;

        Vec3 top{ p.pos.x, p.pos.y + 1.8f, p.pos.z };
        if (!snap->camera.w2s(top, head, (float)W, (float)H)) continue;

        if (!std::isfinite(feet.x) || !std::isfinite(head.y)) continue;

        const CGFloat topY = MIN(head.y, feet.y);
        const CGFloat botY = MAX(head.y, feet.y);
        const CGFloat hPx = botY - topY;
        if (hPx < 2.0 || hPx > H * 2.0) continue;

        const CGFloat wPx = hPx / 4.0;
        const CGRect box = CGRectMake(feet.x - wPx, topY, wPx * 2.0, hPx);

        if (box.origin.x < -W || box.origin.x > 2 * W ||
            box.origin.y < -H || box.origin.y > 2 * H) continue;

        if (g_cfg.filled.load()) {
            CGContextSetRGBFillColor(ctx, 1.0, 0.15, 0.15, 0.12);
            CGContextFillRect(ctx, box);
        }

        if (g_cfg.boxes.load()) {
            CGContextSetRGBStrokeColor(ctx, 1.0, 0.12, 0.12, 0.95);
            CGContextStrokeRect(ctx, box);
            // corner accents
            CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 1.0, 0.9);
            CGContextSetLineWidth(ctx, 2.0);
            const CGFloat c = MAX(4.0, wPx * 0.35);
            CGContextMoveToPoint(ctx, box.origin.x, box.origin.y + c);
            CGContextAddLineToPoint(ctx, box.origin.x, box.origin.y);
            CGContextAddLineToPoint(ctx, box.origin.x + c, box.origin.y);
            CGContextMoveToPoint(ctx, CGRectGetMaxX(box) - c, box.origin.y);
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(box), box.origin.y);
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(box), box.origin.y + c);
            CGContextMoveToPoint(ctx, box.origin.x, CGRectGetMaxY(box) - c);
            CGContextAddLineToPoint(ctx, box.origin.x, CGRectGetMaxY(box));
            CGContextAddLineToPoint(ctx, box.origin.x + c, CGRectGetMaxY(box));
            CGContextMoveToPoint(ctx, CGRectGetMaxX(box) - c, CGRectGetMaxY(box));
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(box), CGRectGetMaxY(box));
            CGContextAddLineToPoint(ctx, CGRectGetMaxX(box), CGRectGetMaxY(box) - c);
            CGContextStrokePath(ctx);
            CGContextSetLineWidth(ctx, 1.0);
        }

        if (g_cfg.distance.load()) {
            NSString* d = [NSString stringWithFormat:@"%.0fm", p.distance];
            [d drawAtPoint:CGPointMake(box.origin.x, box.origin.y - 12)
                withAttributes:@{
                    NSFontAttributeName: [UIFont boldSystemFontOfSize:10],
                    NSForegroundColorAttributeName: [UIColor whiteColor],
                    NSBackgroundColorAttributeName: [UIColor colorWithWhite:0 alpha:0.45]
                }];
        }
    }

    if (g_cfg.status.load()) {
        const char* s = gameStatusLine();
        NSString* st = [NSString stringWithFormat:@"ascent  %s", s];
        [st drawAtPoint:CGPointMake(8, 8)
            withAttributes:@{
                NSFontAttributeName: [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightMedium],
                NSForegroundColorAttributeName: [UIColor colorWithRed:0.4 green:1.0 blue:0.5 alpha:1.0],
                NSBackgroundColorAttributeName: [UIColor colorWithWhite:0 alpha:0.5]
            }];
    }
}

@end

// ---------------------------------------------------------------------------
@interface AscentOverlay : NSObject
@property (nonatomic, strong) UIWindow* window;
@property (nonatomic, strong) AscentDrawView* view;
@property (nonatomic, strong) CADisplayLink* link;
@end

@implementation AscentOverlay

+ (instancetype)shared {
    static AscentOverlay* s = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [AscentOverlay new]; });
    return s;
}

- (void)start {
    UIWindowScene* scene = nil;
    for (UIScene* sc in UIApplication.sharedApplication.connectedScenes) {
        if ([sc isKindOfClass:[UIWindowScene class]]) { scene = (UIWindowScene*)sc; break; }
    }
    if (!scene) return;

    self.window = [[UIWindow alloc] initWithWindowScene:scene];
    self.window.frame = scene.coordinateSpace.bounds;
    self.window.windowLevel = UIWindowLevelAlert + 100;
    self.window.backgroundColor = [UIColor clearColor];
    self.window.opaque = NO;
    self.window.userInteractionEnabled = NO;

    self.view = [[AscentDrawView alloc] initWithFrame:self.window.bounds];
    self.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.window addSubview:self.view];
    self.window.hidden = NO;

    g_screenW.store((float)self.window.bounds.size.width);
    g_screenH.store((float)self.window.bounds.size.height);

    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)tick:(CADisplayLink*)link {
    g_screenW.store((float)self.view.bounds.size.width);
    g_screenH.store((float)self.view.bounds.size.height);
    [self.view setNeedsDisplay];
}

@end

// ---------------------------------------------------------------------------
// Mod menu: floating square + draggable panel, drawn INSIDE the game process.
// Lives in its own window, so it stays on screen above the game (a normal app
// gets frozen by iOS the moment you switch away — only an injected dylib can
// keep drawing over Standoff 2).
@interface AscentPassView : UIView
@end

@implementation AscentPassView
// Everything that is not the square / panel is transparent to touches, so the
// game keeps all of its controls.
- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {
    UIView* hit = [super hitTest:point withEvent:event];
    return (hit == self) ? nil : hit;
}
@end

static UIView* AscentMakeRow(NSString* title, BOOL on, NSInteger tag,
                             id target, SEL action) {
    UIView* row = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 250, 40)];

    UILabel* l = [[UILabel alloc] initWithFrame:CGRectZero];
    l.text = title;
    l.textColor = [UIColor colorWithWhite:0.95 alpha:1.0];
    l.font = [UIFont systemFontOfSize:14 weight:UIFontWeightMedium];
    [l sizeToFit];
    l.center = CGPointMake(18 + l.bounds.size.width / 2.0, 20);
    [row addSubview:l];

    UISwitch* sw = [[UISwitch alloc] initWithFrame:CGRectZero];
    sw.on = on;
    sw.tag = tag;
    sw.transform = CGAffineTransformMakeScale(0.8, 0.8);
    sw.center = CGPointMake(210, 20);
    [sw addTarget:target action:action forControlEvents:UIControlEventValueChanged];
    [row addSubview:sw];
    return row;
}

@interface AscentMenu : NSObject <UIGestureRecognizerDelegate>
@property (nonatomic, strong) UIWindow* window;
@property (nonatomic, strong) UIView* square;
@property (nonatomic, strong) UIView* panel;
@end

@implementation AscentMenu

+ (instancetype)shared {
    static AscentMenu* s = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [AscentMenu new]; });
    return s;
}

- (void)start {
    UIWindowScene* scene = nil;
    for (UIScene* sc in UIApplication.sharedApplication.connectedScenes) {
        if ([sc isKindOfClass:[UIWindowScene class]]) { scene = (UIWindowScene*)sc; break; }
    }
    if (!scene) return;

    self.window = [[UIWindow alloc] initWithWindowScene:scene];
    self.window.frame = scene.coordinateSpace.bounds;
    self.window.windowLevel = UIWindowLevelAlert + 101;
    self.window.backgroundColor = [UIColor clearColor];

    UIViewController* vc = [UIViewController new];
    AscentPassView* root = [[AscentPassView alloc] initWithFrame:self.window.bounds];
    root.backgroundColor = [UIColor clearColor];
    root.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    vc.view = root;
    self.window.rootViewController = vc;

    const CGFloat W = self.window.bounds.size.width;
    const CGFloat H = self.window.bounds.size.height;

    // ---- квадратик ----
    self.square = [[UIView alloc] initWithFrame:CGRectMake(W - 66, H * 0.42, 50, 50)];
    self.square.backgroundColor = [UIColor colorWithWhite:0.10 alpha:0.55];
    self.square.layer.cornerRadius = 14;
    self.square.layer.cornerCurve = kCACornerCurveContinuous;
    self.square.layer.borderWidth = 1.0;
    self.square.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.28].CGColor;
    self.square.layer.shadowColor = [UIColor blackColor].CGColor;
    self.square.layer.shadowOpacity = 0.45;
    self.square.layer.shadowRadius = 8;
    self.square.layer.shadowOffset = CGSizeMake(0, 3);

    UILabel* glyph = [[UILabel alloc] initWithFrame:self.square.bounds];
    glyph.text = @"A";
    glyph.textAlignment = NSTextAlignmentCenter;
    glyph.textColor = [UIColor whiteColor];
    glyph.font = [UIFont systemFontOfSize:24 weight:UIFontWeightHeavy];
    glyph.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.square addSubview:glyph];
    [root addSubview:self.square];

    [self.square addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                             action:@selector(togglePanel)]];
    [self.square addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:self
                                                                             action:@selector(dragSquare:)]];

    // ---- меню ----
    const CGFloat pw = 250, ph = 250;
    self.panel = [[UIView alloc] initWithFrame:CGRectMake(W - pw - 16, H * 0.42 - 20, pw, ph)];
    self.panel.backgroundColor = [UIColor colorWithWhite:0.06 alpha:0.86];
    self.panel.layer.cornerRadius = 18;
    self.panel.layer.cornerCurve = kCACornerCurveContinuous;
    self.panel.layer.borderWidth = 1.0;
    self.panel.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.14].CGColor;
    self.panel.layer.shadowColor = [UIColor blackColor].CGColor;
    self.panel.layer.shadowOpacity = 0.5;
    self.panel.layer.shadowRadius = 14;
    self.panel.layer.shadowOffset = CGSizeMake(0, 6);

    UILabel* title = [[UILabel alloc] initWithFrame:CGRectMake(16, 12, 160, 22)];
    title.text = @"ascent";
    title.textColor = [UIColor whiteColor];
    title.font = [UIFont systemFontOfSize:16 weight:UIFontWeightHeavy];
    [self.panel addSubview:title];

    UIButton* close = [UIButton buttonWithType:UIButtonTypeSystem];
    close.frame = CGRectMake(pw - 44, 8, 36, 30);
    [close setTitle:@"✕" forState:UIControlStateNormal];
    [close setTitleColor:[UIColor colorWithWhite:0.9 alpha:1.0] forState:UIControlStateNormal];
    [close addTarget:self action:@selector(togglePanel) forControlEvents:UIControlEventTouchUpInside];
    [self.panel addSubview:close];

    NSArray<NSString*>* titles = @[@"ESP", @"Boxes", @"Filled", @"Distance", @"Status"];
    CGFloat y = 44;
    for (NSInteger i = 0; i < (NSInteger)titles.count; i++) {
        BOOL on = NO;
        switch (i) {
            case 0: on = g_cfg.enabled.load();  break;
            case 1: on = g_cfg.boxes.load();    break;
            case 2: on = g_cfg.filled.load();   break;
            case 3: on = g_cfg.distance.load(); break;
            case 4: on = g_cfg.status.load();   break;
        }
        UIView* row = AscentMakeRow(titles[i], on, i, self, @selector(rowToggled:));
        row.frame = CGRectMake(0, y, pw, 40);
        [self.panel addSubview:row];
        y += 40;
    }

    [root addSubview:self.panel];
    self.panel.hidden = YES;

    UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc] initWithTarget:self
                                                                         action:@selector(dragPanel:)];
    pan.delegate = self;
    [self.panel addGestureRecognizer:pan];

    self.window.hidden = NO;
}

- (void)togglePanel {
    self.panel.hidden = !self.panel.hidden;
}

- (void)dragSquare:(UIPanGestureRecognizer*)g {
    CGPoint t = [g translationInView:self.window];
    self.square.center = CGPointMake(self.square.center.x + t.x, self.square.center.y + t.y);
    [g setTranslation:CGPointZero inView:self.window];
}

- (void)dragPanel:(UIPanGestureRecognizer*)g {
    CGPoint t = [g translationInView:self.window];
    self.panel.center = CGPointMake(self.panel.center.x + t.x, self.panel.center.y + t.y);
    [g setTranslation:CGPointZero inView:self.window];
}

- (void)rowToggled:(UISwitch*)sw {
    switch (sw.tag) {
        case 0: g_cfg.enabled.store(sw.isOn);  break;
        case 1: g_cfg.boxes.store(sw.isOn);    break;
        case 2: g_cfg.filled.store(sw.isOn);   break;
        case 3: g_cfg.distance.store(sw.isOn); break;
        case 4: g_cfg.status.store(sw.isOn);   break;
    }
}

// Не начинаем тащить панель, если палец попал на переключатель/кнопку.
- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer*)g {
    if (![g isKindOfClass:[UIPanGestureRecognizer class]]) return YES;
    CGPoint p = [g locationInView:self.panel];
    UIView* hit = [self.panel hitTest:p withEvent:nil];
    for (UIView* v = hit; v && v != self.panel; v = v.superview) {
        if ([v isKindOfClass:[UIControl class]]) return NO;
    }
    return YES;
}

@end

// ---------------------------------------------------------------------------
void ascentStartOverlay(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[AscentOverlay shared] start];
        [[AscentMenu shared] start];
    });
}
