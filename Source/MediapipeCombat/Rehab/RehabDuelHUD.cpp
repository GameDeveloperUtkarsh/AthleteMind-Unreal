#include "Rehab/RehabDuel.h"
#include "Rehab/DuelWarden.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

ARehabDuelHUD::ARehabDuelHUD()
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> Atlas(TEXT("/Game/AthleteMind/Guide/T_MovementGuide.T_MovementGuide"));
	GuideAtlas = Atlas.Object;
}

void ARehabDuelHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	// Only the no-argument duel commands registered by the visible buttons are dispatched.
	if (auto* Duel = Cast<ARehabDuelController>(PlayerOwner))
		if (UFunction* Action = Duel->FindFunction(BoxName))
			if (Action->HasAnyFunctionFlags(FUNC_Exec) && Action->NumParms == 0)
				Duel->ProcessEvent(Action, nullptr);
}

void ARehabDuelHUD::DrawHUD()
{
	Super::DrawHUD();
	auto* D = Cast<ARehabDuelController>(PlayerOwner);
	if (!D || !Canvas || !GEngine) return;
	const float S = FMath::Min(Canvas->ClipX/980.f, Canvas->ClipY/600.f);
	if (S <= 0) return;
	const float W = Canvas->ClipX/S;
	const float H = Canvas->ClipY/S;
	const float L = (W-FMath::Min(W,1240.f))*.5f + 32;
	const float R = W-L;
	const FLinearColor Night(.016,.024,.046,.97), Gold(.97,.73,.40), Ivory(.95,.94,.86);
	const FLinearColor Mint(.45,.94,.85), Muted(.71,.77,.84), Slate(.075,.115,.15);
	auto Rect = [&](float X,float Y,float Width,float Height,FLinearColor Color)
	{
		DrawRect(Color,X*S,Y*S,Width*S,Height*S);
	};
	auto Oval = [&](float X,float Y,float RX,float RY,FLinearColor Color)
	{
		Canvas->K2_DrawPolygon(nullptr,FVector2D(X*S,Y*S),FVector2D(RX*S,RY*S),24,Color);
	};
	auto Label = [&](const FString& Text,float X,float Y,float Size,FLinearColor Color,float MaxWidth = 1000.f)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Size > 1.45f ? TEXT("Bold") : TEXT("Regular"), 11.f*Size*S);
		const float Width = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
		if (Width > MaxWidth*S) Font.Size *= MaxWidth*S/Width;
		FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(X*S),FMath::RoundToFloat(Y*S)),FText::FromString(Text),GEngine->GetMediumFont(),Color);
		Item.SlateFontInfo = Font;
		Canvas->DrawItem(Item);
	};
	const FName Focus = D->FocusedAction();
	auto Button = [&](FName Command,const FString& Key,const FString& Title,float X,float Y,float Width,
		FLinearColor Accent,bool Enabled = true)
	{
		const bool Hover = Enabled && HitBoxesOver.Contains(Command);
		const bool Selected = Enabled && Focus == Command;
		Rect(X,Y,Width,40,(Selected || Hover) ? FLinearColor(.16,.23,.27) : Slate);
		if (Selected) { Rect(X,Y,Width,2,Accent); Rect(X,Y+38,Width,2,Accent); Rect(X+Width-2,Y,2,40,Accent); }
		Rect(X,Y,3,40,Enabled ? Accent : Muted*.4f);
		const FString Badge = Selected && D->bPadUsed ? TEXT("A") : Selected && D->bHandsFree && D->GestureMenu() ? TEXT("Hold") : Key;
		Rect(X+10,Y+9,33,22,Night);
		Label(Badge,X+14,Y+11,.9f,Enabled ? Accent : Muted*.6f,27);
		Label(Title,X+55,Y+8,1.23f,Enabled ? Ivory : Muted*.7f,Width-63);
		if (Enabled) AddHitBox(FVector2D(X*S,Y*S), FVector2D(Width*S,40*S),Command,true);
	};
	auto Link = [&](FName Command,const FString& Text,float X,float Y,float Width)
	{
		if (Focus == Command) { Rect(X,Y,Width,26,Slate); Rect(X,Y+24,Width,2,Mint); }
		Label(Text,X+3,Y+4,.93f,HitBoxesOver.Contains(Command) || Focus == Command ? Ivory : Muted,Width-2);
		AddHitBox(FVector2D(X*S,Y*S), FVector2D(Width*S,26*S),Command,true);
	};
	auto Panel = [&](float Height)
	{
		Rect(0,H-Height,W,Height,Night);
		Rect(L,H-Height,R-L,1.3f,Gold);
		return H-Height;
	};

	const auto Step = D->Step;
	const bool Opening = Step == EDuelStep::Intro || Step == EDuelStep::Setup;
	const bool GestureMenu = D->GestureMenu();
	const FString SideName = D->bRightSide ? TEXT("right") : TEXT("left");
	const float Top = 17;
	Rect(L,Top,Opening ? 245 : 227,47,Night);
	const float EX = L+22, EY = Top+23;
	for (int32 I=0; I<4; ++I)
	{
		const float A=I*PI*.5f, B=(I+1)*PI*.5f;
		DrawLine((EX+10*FMath::Cos(A))*S,(EY+14*FMath::Sin(A))*S,(EX+10*FMath::Cos(B))*S,(EY+14*FMath::Sin(B))*S,Gold,1.7f*S);
	}
	Oval(EX,EY,3,5,Gold);
	Label(Opening ? TEXT("DUSK STATION") : (D->bPractice ? TEXT("PRACTICE") : TEXT("WEBCAM")),L+44,Top+4,.83f,Gold);
	if (Opening) Label(TEXT("The Last Beacon"),L+44,Top+18,1.4f,Ivory);
	if (Opening)
	{
		
		const float Y = Panel(168);
		if (Step == EDuelStep::Intro)
		{
			Label(TEXT("The city's last relay has gone dark."),L,Y+12,2.05f,Ivory);
			Label(TEXT("Choose a technique. Follow its movement card to power your Keeper. Try the pictures in practice first."),L,Y+56,1.10f,Muted);
			Button("DuelPractice",TEXT("Enter"),TEXT("Play practice battle"),L,Y+96,270,Mint);
			Button("DuelLive",TEXT("C"),TEXT("Play with webcam"),L+286,Y+96,260,Gold);
			Link("DuelMotion",D->bCameraMotion ? TEXT("M   Camera motion on") : TEXT("M   Camera motion off"),L,Y+139,199);
		}
		else
		{
			const bool Ready = D->HasKneeSignal();
			Link("DuelSide",D->bRightSide ? TEXT("X    Measuring right   /   Switch") : TEXT("X    Measuring left   /   Switch"),R-217,Y+13,217);
			const bool Open = D->CameraProcess.IsValid();
			const auto Signal = D->Bridge ? D->Bridge->GetTrackingState() : EPoseBridgeState::Stopped;
			const bool Streaming = Signal == EPoseBridgeState::Tracked || Signal == EPoseBridgeState::NoPose;
			const FString Side = D->bRightSide ? TEXT("right") : TEXT("left");
			Label(Ready ? TEXT("Your leg is in view. Ready to play?") : TEXT("Set up your webcam"),L,Y+10,1.9f,Ivory);
			Label(FString::Printf(TEXT("Turn your %s side toward the camera. Keep your hip, knee and ankle in view."),*Side),L,Y+50,1.15f,Muted);
			FString Status = D->Outcome;
			if (Ready) Status = !D->bLearnStartingStance || D->SetupNeutral >= 0 ? TEXT("Ready. Choose a way to play.") : TEXT("Leg in view. Hold your usual starting stance for a moment.");
			else if (Signal == EPoseBridgeState::Stopped) Status = TEXT("Receiver offline. Close other running game windows and reopen this battle.");
			else if (Streaming) Status = FString::Printf(TEXT("Step back until your %s ankle is in frame. Use X if the other leg faces the camera."),*Side);
			else if (Status.IsEmpty()) Status = TEXT("Select Start webcam to connect your camera.");
			Label(Status,L,Y+78,1.f,Ready ? Mint : Gold);
			if (Ready)
			{
				Button("DuelHandsFree",TEXT("H"),TEXT("Hands-free battle"),L,Y+111,242,Mint);
				Button("DuelContinue",TEXT("Enter"),TEXT("Use buttons"),L+254,Y+111,215,Gold);
			}
			else Button("DuelConnect",TEXT("W"),Open || Streaming ? TEXT("Connecting camera") : TEXT("Start webcam"),L,Y+111,260,Gold,!Open && !Streaming && Signal != EPoseBridgeState::Stopped);
			Button("DuelPractice",TEXT("B"),TEXT("Practice"),L+(Ready ? 484 : 276),Y+111,165,Muted);
			Rect(L,Y-45,R-L,43,Night);
			Label(TEXT("Webcam sessions use Gemini for an end-of-set recap when configured. Only session results are sent."),L+8,Y-43,.98f,Muted,R-L-16);
			if (Ready) Label(TEXT("Lower your hand, then hold it near your shoulder to select."),L+8,Y-23,1.f,Ivory,R-L-16);
		}
		return;
	}

	const int32 Done = D->PracticeTurns+D->Reps.Num();
	const auto ActiveStep = Step == EDuelStep::Paused ? D->ResumeStep : Step;
	const int32 Turn = FMath::Min(D->TurnLimit(),Done+D->Skipped + (!D->bEnemyTurn && (ActiveStep == EDuelStep::Choose || ActiveStep == EDuelStep::Move) ? 1 : 0));
	Label(FString::Printf(TEXT("Round %d of %d"), FMath::Max(1,Turn), D->TurnLimit()),L+44,Top+19,1.38f,Ivory);
	Rect(R-236,Top,236,47,Night);
	Label(TEXT("Stone Golem"),R-224,Top+3,1.15f,Ivory);
	const float Contact = ADuelWarden::ContactFraction(D->bEnemyTurn,D->Move)*ADuelWarden::StrikeDuration;
	const float Age = D->ResolveTime-Contact;
	const bool Striking = Step == EDuelStep::Resolve && !D->bEnemyTurn && D->LastDamage>0;
	const int32 DisplayHull = D->Hull+(Striking && Age<0 ? D->LastDamage : 0);
	Label(FString::Printf(TEXT("%d / %d"),DisplayHull,D->MaxHull),R-65,Top+5,1.f,Gold);
	Rect(R-224,Top+32,210,4,Slate);
	if (D->MaxHull > 0)
	{
		const float Unit = 210.f/D->MaxHull;
		if (Striking && Age>=0)
		{
			const float Trail = 1-FMath::SmoothStep(0.f,.44f,Age);
			Rect(R-224,Top+32,Unit*(D->Hull+D->LastDamage*Trail),4,Ivory);
		}
		Rect(R-224,Top+32,Unit*DisplayHull,4,Gold);
	}

	const bool Dodge = D->bEnemyTurn;
	const bool Ward = D->Move == EDuelMove::Ward;
	const bool NextIsDodge = Step == EDuelStep::Rest && !Dodge && !Ward && D->LastDamage>0;
	const bool ShowCurl = !Dodge && (Step == EDuelStep::Choose ? Focus == "DuelWard" : Ward);
	const bool ShowLift = !Dodge && (Step == EDuelStep::Choose ? Focus == "DuelFlare" : D->Move == EDuelMove::Flare);
	const bool Coaching = Step == EDuelStep::Move;
	const int32 Phase = D->bPractice ? D->PreviewPhase : static_cast<int32>(D->Cycle.Phase);
	const float Height = Step == EDuelStep::Choose ? 189.f : Coaching ? 173.f : Step == EDuelStep::Finished && !D->bPractice ? 320.f : 166.f;
	const float Y = H-Height-15;
	const float CardWidth = Step == EDuelStep::Choose ? R-L : FMath::Min(R-L, Coaching ? 672.f : 720.f);
	Rect(L,Y,CardWidth,Height,Night);
	Rect(L,Y,3,Height, Dodge ? Gold : Mint);
	Rect(L+3,Y,CardWidth-3,1,Slate);
	const float TextX = L+(Coaching ? 130.f : 18.f);
	const float TextWidth = L+CardWidth-TextX-20;
	FString Kicker, Title, Detail;
	switch (Step)
	{
	case EDuelStep::Choose:
		Kicker = FString::Printf(TEXT("YOUR TURN     /     %d CHARGE"),D->Charge);
		Title = D->bHandsFree && Focus == "DuelGuard" ? TEXT("Squat to cast Relay Strike") : TEXT("Choose an attack.");
		if (ShowCurl) Detail = TEXT("Elbow curl   /   Break 1 armor and block the next pulse.");
		else if (ShowLift) Detail = TEXT("Arm lift   /   Break 1 armor and gain 2 charge.");
		else if (Focus == "DuelCounter") Detail = FString::Printf(TEXT("Squat   /   Spend 1 charge to break %d armor."),D->DamageFor(EDuelMove::Charged));
		else if (D->bHandsFree && Focus == "DuelGuard") Detail = TEXT("Start standing, bend and return. Or browse for a different move.");
		else Detail = FString::Printf(TEXT("Squat   /   Break %d armor and gain 1 charge."),D->DamageFor(EDuelMove::Strike));
		break;
	case EDuelStep::Move:
		Kicker = Dodge ? TEXT("GOLEM'S TURN     /     DODGE") : ShowCurl ? TEXT("YOUR TURN     /     PULSE WARD") : ShowLift ? TEXT("YOUR TURN     /     SIGNAL FLARE") : TEXT("YOUR TURN     /     ATTACK");
		if (Dodge)
		{
			Title = TEXT("Hold your starting stance.");
			Detail = TEXT("Keep your measured knee steady. The Keeper dodges for you.");
		}
		else
		{
			const TCHAR* Cues[] = {TEXT("Start in your usual stance."),TEXT("Lower into your squat range."),TEXT("Hold that position."),TEXT("Stand back up to attack.")};
			Title = !D->bPractice && D->Cycle.bBeyondTarget ? TEXT("Return to your starting stance.") : Cues[Phase];
			Detail = Phase == 3 ? TEXT("Finish in your starting position. Your Keeper will strike.") : TEXT("Move at your own pace.");
			if (ShowCurl || ShowLift)
			{
				const TCHAR* ArmCues[] = { ShowCurl ? TEXT("Start with your elbow relaxed and arm down.") : TEXT("Start with your arm by your side."), ShowCurl ? TEXT("Bend your elbow.") : TEXT("Lift your arm forward."), TEXT("Hold that position."), TEXT("Lower your arm to cast.") };
				Title = D->Cycle.bBeyondTarget ? TEXT("Lower your arm and start again.") : ArmCues[Phase];
				Detail = FString::Printf(TEXT("Use your %s arm. %s"), *SideName, ShowCurl ? TEXT("Keep your upper arm at your side.") : TEXT("Lift only into your set range."));
			}
		}
		if (!D->bPractice)
		{
			float Angle = 0;
			if (!D->ReadJoint(Angle) && (D->LastCapture < 0 || FPlatformTime::Seconds()-D->LastArrival > .20))
			{
				Title = TEXT("Bring your measured side into view.");
				Detail = FString::Printf(TEXT("Face sideways with your %s %s visible."), *SideName,
					ShowCurl ? TEXT("hip, shoulder, elbow and wrist") : ShowLift ? TEXT("hip, shoulder and elbow") : TEXT("hip, knee and ankle"));
			}
			else if (!D->PoseCorrection.IsEmpty()) { Title = D->PoseCorrection; Detail = TEXT("Return to the starting pose to try again."); }
			else if (D->bRecoveringSignal) { Title = TEXT("Return to your starting position."); Detail = TEXT("Your turn continues automatically."); }
		}
		break;
	case EDuelStep::Resolve:
		Kicker = Dodge ? TEXT("GOLEM'S TURN     /     PULSE SWEEP") : TEXT("YOUR TURN     /     STRIKE");
		Title = Age < 0 ? (Dodge ? TEXT("Your Keeper dodges.") : TEXT("Your Keeper attacks.")) : D->Outcome;
		Detail = Dodge ? TEXT("Rest while the pulse passes.") : FString::Printf(TEXT("The golem loses %d armor."),D->LastDamage);
		break;
	case EDuelStep::Rest:
		Kicker = TEXT("BETWEEN TURNS");
		Title = NextIsDodge ? TEXT("Rest before the golem's turn.") : TEXT("Take a breather.");
		Detail = Ward && !Dodge && D->LastDamage > 0 ? TEXT("Your ward blocks the pulse. Your next turn is ready.") : D->Outcome;
		if (D->LastDamage > 0) Detail += FString::Printf(TEXT("   /   %d armor removed"),D->LastDamage);
		break;
	case EDuelStep::Paused:
		Kicker = TEXT("BATTLE PAUSED");
		Title = TEXT("Continue when you're ready.");
		Detail = D->Outcome;
		break;
	default:
		Kicker = D->bPractice ? TEXT("PRACTICE RESULTS") : TEXT("BATTLE RESULTS");
		Title = D->Hull == 0 ? TEXT("The beacon shines again.") : TEXT("Back at camp.");
		Detail = FString::Printf(TEXT("%d of %d %s completed    /    %d skipped    /    %d armor removed"),Done,D->TurnLimit(),
			D->bPractice ? TEXT("practice turns") : TEXT("reps"), D->Skipped,D->MaxHull-D->Hull);
		break;
	}
	if (Coaching && D->PauseStart >= 0) { Title=TEXT("Hold to pause."); Detail=TEXT("Keep your other hand near its shoulder."); }
	Label(Kicker,TextX,Y+11,.91f,Dodge ? Gold : Mint,TextWidth);
	Label(Title,TextX,Y+30,1.95f,Ivory,TextWidth);
	Label(Detail,TextX,Y+66,1.12f,Muted,TextWidth);
	if (Coaching)
	{
		const FLinearColor Accent = Dodge ? Gold : Mint;
		const float Left=L+14;
		const int32 I = Phase == 3 ? 2 : Phase == 0 ? 0 : 1;
		const float Stance = !D->bPractice && D->bLearnStartingStance ? FMath::Max(0.f,D->SetupNeutral) : 0.f;
		const float PictureAngle = !Dodge && I==1 ? D->Profile.TargetMin : Stance;
		const int32 Frame = FMath::Clamp(FMath::RoundToInt(PictureAngle/5),0,15);
		Rect(Left,Y+12,101,Height-24,Slate);
		if (ShowCurl || ShowLift)
		{
			const float GuideX = Left+34, GuideY = Y+48;
			auto Bone = [&](float AX,float AY,float BX,float BY,FLinearColor Color,float Thick)
			{ DrawLine((GuideX+AX)*S,(GuideY+AY)*S,(GuideX+BX)*S,(GuideY+BY)*S,Color,Thick*S); };
			const float Angle = I == 1 ? FMath::DegreesToRadians(D->MovementProfile().TargetMin) : 0.f;
			const float UpperAngle = ShowLift ? Angle : 0.f;
			const float ElbowX = 22.f*FMath::Sin(UpperAngle);
			const float ElbowY = 19.f + 22.f*FMath::Cos(UpperAngle);
			auto Joint = [&](float XJ,float YJ,float RX,float RY,FLinearColor Color)
			{ Oval(GuideX+XJ,GuideY+YJ,RX,RY,Color); };
			// Side view. The far leg provides orientation and the active arm is highlighted.
			const FLinearColor Far(.27,.34,.41);
			Bone(-6,57,-10,76,Far,7); Bone(-10,76,-10,94,Far,5); Bone(-11,94,-4,94,Far,4);
			Bone(-2,56,1,76,Muted,8); Joint(1,76,4,4,Muted);
			Bone(1,76,-1,94,Muted,6); Bone(-2,95,8,95,Muted,4);
			Joint(-3,54,7,8,Muted); Bone(-4,22,-4,54,Muted,11);
			Joint(-3,29,8,13,Muted); Bone(-2,10,-2,21,Ivory,5);
			Joint(-2,7,6.3f,8.4f,Ivory); Joint(3.4f,8,2.7f,2,Ivory);
			Joint(0,19,4,4,Accent); Bone(0,19,ElbowX,ElbowY,Accent,6);
			const float WX = ElbowX + 21.f*FMath::Sin(Angle);
			const float WY = ElbowY + 21.f*FMath::Cos(Angle);
			Joint(ElbowX,ElbowY,3,3,Accent); Bone(ElbowX,ElbowY,WX,WY,Accent,4);
			Joint(WX,WY,2.8f,3.4f,Ivory);
		}
		else if (GuideAtlas) DrawTexture(GuideAtlas,(Left+7)*S,(Y+37)*S,84*S,116*S,
			(Frame%4+.14f)/4,(Frame/4+.01f)/4,.72f/4,.98f/4,
			FLinearColor::White,BLEND_Translucent);
		if (!D->bPractice)
		{
			const int32 Ms = Dodge ? D->Profile.DodgeHoldMs : D->MovementProfile().TargetHoldMs;
			const float Fill = Dodge || Phase==2 ? D->Cycle.HoldFraction(D->LastCapture,Ms) : 0.f;
			const int32 Count = Dodge ? 1 : 4;
			const float Segment=(TextWidth+4)/Count;
			for (int32 P=0;P<Count;++P)
			{
				const float X = TextX+P*Segment;
				Rect(X,Y+103,Segment-4,5,Slate);
				const float Amount = Dodge ? Fill : P<Phase ? 1.f : P==Phase ? FMath::Max(.08f,Fill) : 0.f;
				Rect(X,Y+103,(Segment-4)*Amount,5,Accent);
			}
		}
	}

	const float Row = Y+91;
	switch (Step)
	{
	case EDuelStep::Choose:
	{
		const float Width = (CardWidth-54)/4;
		Button("DuelGuard",TEXT("1"),TEXT("Relay strike"),L+18,Row,Width,Mint);
		Button("DuelCounter",TEXT("2"),D->Charge ? TEXT("Charged attack") : TEXT("No charge"),L+24+Width,Row,Width,Gold,D->Charge>0);
		if (D->bEnableArmMoves && D->CurlProfile.IsValid()) Button("DuelWard",TEXT("3"),TEXT("Pulse ward"),L+30+2*Width,Row,Width,Mint);
		if (D->bEnableArmMoves && D->LiftProfile.IsValid()) Button("DuelFlare",TEXT("4"),TEXT("Signal flare"),L+36+3*Width,Row,Width,Gold);
		break;
	}
	case EDuelStep::Move:
		if (D->bPractice)
		{
			Button("DuelContinue",TEXT("Enter"),Dodge ? TEXT("Preview dodge") : Phase == 3 ? TEXT("Preview attack") : TEXT("Next picture"),TextX,Row,200,Dodge ? Gold : Mint);
		}
		break;
	case EDuelStep::Paused: Button("DuelContinue",TEXT("Enter"),TEXT("Resume battle"),TextX,Row,235,Mint); break;
	case EDuelStep::Rest: Button("DuelContinue",TEXT("Enter"),NextIsDodge ? TEXT("Ready to dodge") : TEXT("Ready to attack"),TextX,Row,250,Mint); break;
	case EDuelStep::Finished:
		Button("DuelRestart",TEXT("R"),TEXT("Return to camp"),TextX,Row,240,Gold);
		Label(D->bPractice ? TEXT("Practice only. No exercise record saved.") : (D->bSaved ? D->FinishReason + TEXT("   /   Session saved") : TEXT("Save failed. Press R to retry.")),TextX+249,Row+11,1.f,Muted);
		if (!D->bPractice)
		{
			Label(TEXT("GEMINI SESSION REPORT"),TextX,Y+148,.98f,Gold);
			TArray<FString> Words;
			D->ReportText.ParseIntoArrayWS(Words);
			FString Line;
			float LineY = Y+170;
			for (const FString& Word : Words)
			{
				if (Line.Len()+Word.Len() > 64 && !Line.IsEmpty())
				{
					Label(Line,TextX,LineY,1.1f,Ivory,TextWidth);
					LineY += 19;
					Line.Reset();
					if (LineY > Y+258) { Line = TEXT("Full report saved in Saved/RehabSessions."); break; }
				}
				if (!Line.IsEmpty()) Line += TEXT(" ");
				Line += Word;
			}
			Label(Line,TextX,LineY,1.1f,Ivory,TextWidth);
			if (!D->ReportProcess.IsValid() && !D->bReportReady)
				Link("DuelReport",TEXT("G   Retry report"),TextX,Y+282,132);
		}
		break;
	default: break;
	}
	if (Step != EDuelStep::Finished)
	{
		const float Foot = H-46;
		if (Step != EDuelStep::Resolve && Step != EDuelStep::Paused) Link("DuelPause",TEXT("P   Pause"),TextX,Foot,85);
		if (Step == EDuelStep::Move || (Step == EDuelStep::Paused && D->ResumeStep == EDuelStep::Move))
			Link("DuelSkip",TEXT("K   Skip turn"),TextX+90,Foot,110);
		if (Step == EDuelStep::Paused || Step == EDuelStep::Resolve) Link("DuelFinish",TEXT("F   End battle"),TextX+226,Foot,119);
		if (!D->bPractice) Link("DuelDiscomfort",TEXT("D   Stop"),L+CardWidth-88,Foot,78);
		if (Step == EDuelStep::Paused)
			Link("DuelMotion",D->bCameraMotion ? TEXT("M   Camera motion on") : TEXT("M   Camera motion off"),TextX+365,Foot,169);
		if (GestureMenu)
		{
			const FString Hint = FString::Printf(TEXT("%s hand: reach to browse, hand by shoulder to select. Lower between holds."),D->bRightSide ? TEXT("Right") : TEXT("Left"));
			const float HintY = Y-30;
			Rect(L,HintY,CardWidth,26,Night);
			Label(Hint,L+12,HintY+3,.98f,Muted,CardWidth-28);
			Rect(L,HintY+24,CardWidth*D->GestureFill,2,Mint);
		}
	}
}
